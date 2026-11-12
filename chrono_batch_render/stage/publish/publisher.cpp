// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/stage/publish/publisher.h"

#include <algorithm>
#include <optional>

#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/stage/columns/kernels/changes.h"
#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/util/log.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {

void Publisher::Finish() {
    for (const Scheduled& scheduled : m_scheduled) {
        scheduled.table->Finish();
    }
}

bool Publisher::Pending() const {
    return m_structure.GetColumnStore().WriteSequence() != m_writes ||
           m_structure.Epoch() != m_epoch ||
           std::ranges::any_of(
               m_scheduled, [](const Scheduled& scheduled) { return scheduled.table->Pending(); });
}

void Publisher::Add(Table& table) {
    for (const TableId output : table.TableOutputs()) {
        for (const TableId input : table.TableInputs()) {
            if (input == output) {
                throw Error("Table {} reads its own output", table.Name());
            }
        }
        for (const Scheduled& earlier : m_scheduled) {
            for (const TableId input : earlier.table->TableInputs()) {
                if (input == output) {
                    throw Error("Table {} writes a table {} reads, so add {} first", table.Name(),
                                earlier.table->Name(), table.Name());
                }
            }
        }
    }
    for (const ColumnRef column : table.RowInputs()) {
        m_structure.GetColumnStore().TrackRows(column);
    }
    m_scheduled.push_back({&table});
}

Ordinal Publisher::SealedStamp(Ordinal ordinal) const {
    const auto after = std::ranges::upper_bound(m_sealed, ordinal, {}, &Sealed::ordinal);
    return after == m_sealed.begin() ? 0 : std::prev(after)->stamp;
}

Ordinal Publisher::SealedOrdinal(Ordinal stamp, Ordinal sealing) const {
    const auto found = std::ranges::lower_bound(m_sealed, stamp, {}, &Sealed::stamp);
    return found == m_sealed.end() ? sealing : found->ordinal;
}

ChangeSet Publisher::Build(Ordinal since, Ordinal until, Ordinal sealing,
                           std::span<const ColumnRef> columns, std::span<const ColumnRef> rows,
                           std::vector<Ref<device::Buffer>>* reused) {
    CHRONO_BATCH_RENDER_ZONE("Publisher/Changes");
    ColumnStore& store = m_structure.GetColumnStore();
    ChangeSet changes;
    changes.since = since == 0 ? 0 : SealedOrdinal(since, sealing);
    changes.until = SealedOrdinal(until, sealing);
    changes.sinceStamp = since;
    changes.untilStamp = until;
    changes.resync = since == 0;
    {
        CHRONO_BATCH_RENDER_ZONE("Publisher/StructureLog");
        const std::span<const StructureEntry> log = m_structure.Log();
        for (auto at = std::ranges::upper_bound(log, since, {}, &StructureEntry::ordinal);
             at != log.end() && at->ordinal <= until; ++at) {
            StructureEntry entry = *at;
            entry.ordinal = SealedOrdinal(entry.ordinal, sealing);
            if (entry.api != SchemaId::None) {
                changes.apis.push_back(entry);
            } else {
                (entry.added ? changes.added : changes.removed).push_back(entry);
            }
        }
        changes.structure = !changes.added.empty() || !changes.removed.empty();
    }
    {
        CHRONO_BATCH_RENDER_ZONE("Publisher/Worlds");
        const uint32_t worlds = m_structure.GetPrims().WorldCount();
        for (uint32_t slot = 0; slot <= worlds; ++slot) {
            const uint32_t world = SlotWorld(slot);
            const Ordinal stamp = store.WorldStamp(world);
            if (stamp > since && stamp <= until) {
                changes.worlds.push_back(world);
            }
        }
        std::ranges::sort(changes.worlds);
    }
    {
        CHRONO_BATCH_RENDER_ZONE("Publisher/ArrayRows");
        for (const ColumnRef column : columns) {
            if (IsArray(column.column->type)) {
                changes.arrays.emplace_back(column, store.ArrayRows(column, since, until));
            }
        }
    }
    if (rows.empty()) {
        return changes;
    }
    CHRONO_BATCH_RENDER_ZONE("Publisher/CompactRows");
    if (reused != nullptr) {
        reused->resize(rows.size());
    }
    device::Queue& queue = store.GetDevice().GetQueue();
    std::optional<device::CommandEncoder> encoder;
    for (size_t i = 0; i < rows.size(); ++i) {
        const ColumnRef column = rows[i];
        store.TrackRows(column);
        const ColumnTable& table = store.Schema(column.schema);
        const uint32_t count = table.Rows();
        const uint64_t bytes =
            (uint64_t(kChangedRowsOffset) + std::max(count, 1u)) * sizeof(uint32_t);
        Ref<device::Buffer> buffer = reused != nullptr ? (*reused)[i] : nullptr;
        if (!buffer || buffer->Bytes() < bytes) {
            buffer = store.GetDevice().CreateBuffer({.bytes = bytes, .label = "changed rows"});
            if (reused != nullptr) {
                (*reused)[i] = buffer;
            }
        }
        if (!encoder) {
            encoder.emplace(queue.CreateEncoder());
        }
        encoder->Fill(buffer->Slice(0, kChangedRowsOffset * sizeof(uint32_t)));
        encoder->Barrier();
        CompactParams params{};
        params.stamps.address = table.Pages(store.StampColumn(column));
        params.tablePrims.address = table.Pages(store.PrimsColumn(column.schema));
        params.out.address = buffer->Address();
        params.since = since;
        params.until = until;
        params.rows = count;
        store.Record(*encoder, "CompactChanged", params, count);
        changes.columns.emplace_back(column, buffer, count);
    }
    if (encoder) {
        queue.Submit(encoder->Finish());
    }
    return changes;
}

bool Publisher::RunTables(Ordinal stamp, Ordinal ordinal, std::span<Table* const> only) {
    ColumnStore& store = m_structure.GetColumnStore();
    bool ran = false;
    for (Scheduled& scheduled : m_scheduled) {
        Table& table = *scheduled.table;
        if (!only.empty() && std::ranges::find(only, &table) == only.end()) {
            continue;
        }
        const bool written = std::ranges::any_of(table.Inputs(), [&](ColumnRef input) {
            return store.LastWrite(input) > scheduled.writes;
        });
        const bool restructured = table.Structural() && m_structure.Epoch() != scheduled.epoch;
        const bool tabled = std::ranges::any_of(table.TableInputs(), [&](TableId id) {
            return m_versions.Version(id) > scheduled.logged;
        });
        if (scheduled.ran && !written && !restructured && !tabled && !table.Pending()) {
            continue;
        }
        CHRONO_BATCH_RENDER_ZONE("Publisher/Table");
        ChangeSet changes = Build(scheduled.ran ? scheduled.stamp : 0, stamp, ordinal,
                                  table.Inputs(), table.RowInputs(), &scheduled.changed);
        {
            CHRONO_BATCH_RENDER_ZONE("Publisher/Versions");
            for (const TableId id : table.TableInputs()) {
                if (m_versions.Version(id) > scheduled.logged) {
                    changes.wrote.push_back(id);
                    m_versions.Collect(id, scheduled.logged, changes.worlds);
                }
            }
        }
        const uint64_t writes = store.WriteSequence();
        const uint64_t epoch = m_structure.Epoch();
        const uint64_t logged = m_versions.Sequence();
        ++table.m_runs;
        ++m_tableRuns;
        table.Run(changes, m_versions);
        if (m_header != nullptr) {
            m_header->Describe(table);
        }
        scheduled.writes = writes;
        scheduled.epoch = epoch;
        scheduled.logged = logged;
        scheduled.stamp = stamp;
        scheduled.ran = true;
        ran = true;
    }
    return ran;
}

void Publisher::Trim(Ordinal stamp) {
    CHRONO_BATCH_RENDER_ZONE("Publisher/Trim");
    ColumnStore& store = m_structure.GetColumnStore();
    Ordinal seen = stamp;
    std::vector<std::pair<ColumnRef, Ordinal>> held;
    for (const Scheduled& scheduled : m_scheduled) {
        if (scheduled.table->Structural()) {
            seen = std::min(seen, scheduled.stamp);
        }
        for (const ColumnRef column : scheduled.table->Inputs()) {
            if (!IsArray(column.column->type)) {
                continue;
            }
            const auto found = std::ranges::find_if(
                held, [&](const auto& entry) { return entry.first.column == column.column; });
            if (found == held.end()) {
                held.emplace_back(column, scheduled.stamp);
            } else {
                found->second = std::min(found->second, scheduled.stamp);
            }
        }
    }
    store.TrimArrayWrites(stamp, held);
    if (!m_scheduled.empty()) {
        m_structure.TrimLog(seen);
    }
    uint64_t logged = m_versions.Sequence();
    for (const Scheduled& scheduled : m_scheduled) {
        if (!scheduled.table->TableInputs().empty()) {
            logged = std::min(logged, scheduled.logged);
        }
    }
    m_versions.Trim(logged);
}

device::Operation Publisher::Publish(Ordinal ordinal, Stream producer) {
    CheckStoreLock(m_structure.GetColumnStore().Lock(), "Publishing a scene");
    CHRONO_BATCH_RENDER_ZONE("Publisher/Publish");
    if (!m_sealed.empty() && ordinal <= m_sealed.back().ordinal) {
        throw Error("Publishing ordinal {}, but {} is already published", ordinal,
                    m_sealed.back().ordinal);
    }
    ColumnStore& store = m_structure.GetColumnStore();
    device::Queue& queue = store.GetDevice().GetQueue();
    queue.Follow(producer);
    const Ordinal stamp = store.Stamp();
    device::Operation done;
    if (Pending()) {
        queue.BeginBatch();
        try {
            store.Flush();
            RunTables(stamp, ordinal);
            Trim(stamp);
            Seal();
            store.DeviceData();
        } catch (...) {
            queue.EndBatch();
            store.SetStamp(stamp + 1);
            throw;
        }
        done = queue.EndBatch();
    }
    m_sealed.push_back({ordinal, stamp});
    if (m_sealed.size() > 2 * kSealedHistory) {
        m_sealed.erase(m_sealed.begin(), m_sealed.end() - ptrdiff_t(kSealedHistory));
    }
    store.SetStamp(stamp + 1);
    m_writes = store.WriteSequence();
    m_epoch = m_structure.Epoch();
    return done;
}

device::Operation Publisher::Refresh(std::span<Table* const> tables) {
    CheckStoreLock(m_structure.GetColumnStore().Lock(), "Refreshing a scene's tables");
    CHRONO_BATCH_RENDER_ZONE("Publisher/Refresh");
    ColumnStore& store = m_structure.GetColumnStore();
    device::Queue& queue = store.GetDevice().GetQueue();
    const Ordinal stamp = store.Stamp();
    queue.BeginBatch();
    bool ran = false;
    try {
        store.Flush();
        ran = RunTables(stamp, Published() + 1, tables);
        store.DeviceData();
    } catch (...) {
        queue.EndBatch();
        store.SetStamp(stamp + 1);
        throw;
    }
    const device::Operation done = queue.EndBatch();
    if (ran) {
        store.SetStamp(stamp + 1);
    }
    return done;
}

ChangeSet Publisher::Changes(Ordinal since, std::span<const ColumnRef> columns) {
    if (since > Published()) {
        throw Error("Changes since {}, but only {} is published", since, Published());
    }
    const Ordinal until = m_sealed.empty() ? 0 : m_sealed.back().stamp;
    return Build(SealedStamp(since), until, Published(), columns, columns);
}

void Publisher::Seal() {
    CHRONO_BATCH_RENDER_ZONE("Publisher/Seal");
    auto pinned = std::make_shared<PinnedTables>();
    if (m_header != nullptr) {
        const std::span<const std::byte> header = m_header->Seal(
            m_structure.GetColumnStore().Data(), m_structure.GetPrims().WorldCount());
        pinned->header.assign(header.begin(), header.end());
    }
    if (m_pinned && m_pinned->header == pinned->header) {
        pinned->buffer = m_pinned->buffer;
    } else if (!pinned->header.empty()) {
        pinned->buffer = m_structure.GetColumnStore().GetDevice().CreateBuffer(
            {.label = "snapshot header"}, std::span<const std::byte>(pinned->header));
    }
    if (pinned->buffer) {
        pinned->held.emplace_back(pinned->buffer);
    }
    pinned->content = m_tableRuns;
    for (const Scheduled& scheduled : m_scheduled) {
        scheduled.table->Hold(pinned->held);
        pinned->versions.emplace_back(scheduled.table->Name(), scheduled.table->Runs());
    }
    m_pinned = std::move(pinned);
}

Snapshot Publisher::Acquire(Stream consumer) {
    if (m_sealed.empty()) {
        throw Error("Nothing is published yet");
    }
    ColumnStore& store = m_structure.GetColumnStore();
    Snapshot snapshot;
    snapshot.ordinal = Published();
    store.DeviceData();
    if (!m_pinned) {
        Seal();
    }
    snapshot.tables = m_pinned->buffer ? m_pinned->buffer->Address() : 0;
    snapshot.pinned = m_pinned;
    snapshot.ready = store.GetDevice().GetQueue().Mark();
    snapshot.ready.Wait(consumer);
    ++m_outstanding;
    return snapshot;
}

void Publisher::Release(const Snapshot& snapshot, Stream consumer) {
    if (m_outstanding == 0 || snapshot.ordinal == 0) {
        throw Error("Releasing a snapshot that wasn't acquired");
    }
    m_structure.GetColumnStore().GetDevice().GetQueue().Follow(consumer);
    --m_outstanding;
}

uint64_t Snapshot::Version() const {
    return pinned ? PinnedTables::From(*this).content : 0;
}

uint64_t Snapshot::Version(std::string_view table) const {
    if (!pinned) {
        return 0;
    }
    const PinnedTables& held = PinnedTables::From(*this);
    for (const auto& [name, version] : held.versions) {
        if (name == table) {
            return version;
        }
    }
    std::string names;
    for (const auto& [name, version] : held.versions) {
        names += (names.empty() ? "" : ", ") + std::string(name);
    }
    throw Error("A snapshot has no table \"{}\", only {}", table, names);
}

}
