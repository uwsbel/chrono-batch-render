// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/scene/tables/points.h"

#include <algorithm>
#include <bit>

#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/scene/kernels.h"
#include "chrono_batch_render/scene/records/render_tables.h"
#include "chrono_batch_render/scene/schema/columns.h"
#include "chrono_batch_render/scene/tables/kernels/points.h"
#include "chrono_batch_render/stage/publish/change_set.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {

namespace {

constexpr ColumnRef kInputs[] = {kPointBasedPoints, kPointBasedNormals, kPointsWidths, kPointsIds};

constexpr TableId kTableOutputs[] = {TableId::Points, TableId::PointsMoved};

}

std::span<const ColumnRef> PointsTable::Inputs() const {
    return kInputs;
}

std::span<const TableId> PointsTable::TableOutputs() const {
    return kTableOutputs;
}

void PointsTable::Describe(RenderTables& tables) const {
    tables.points.address = m_records.Buffer() ? m_records.Buffer()->Address() : 0;
}

void PointsTable::Place(uint32_t prim) {
    const Prims& prims = m_structure.GetPrims();
    Cloud cloud;
    cloud.prim = prim;
    cloud.generation = prims.Generation(prim);
    cloud.pointBasedRow = prims.Row(prim, SchemaId::PointBased);
    cloud.row = prims.Row(prim, SchemaId::Points);
    m_byPointBasedRow[cloud.pointBasedRow] = prim;
    m_byRow[cloud.row] = prim;
    Link(m_clouds[prim] = std::move(cloud));
}

void PointsTable::Forget(uint32_t prim) {
    Cloud& cloud = m_clouds.at(prim);
    cloud.bvh.Release(m_structure.GetColumnStore());
    m_records.Set(cloud.row, PointsRecord{});
    m_byPointBasedRow.erase(cloud.pointBasedRow);
    m_byRow.erase(cloud.row);
    m_clouds.erase(prim);
}

void PointsTable::Link(Cloud& cloud) {
    ColumnStore& store = m_structure.GetColumnStore();
    const ArrayRef points = store.Array(kPointBasedPoints, cloud.pointBasedRow);
    const ArrayRef widths = store.Array(kPointsWidths, cloud.row);
    const ArrayRef normals = store.Array(kPointBasedNormals, cloud.pointBasedRow);
    const ArrayRef ids = store.Array(kPointsIds, cloud.row);
    if (points.count > cloud.capacity || points.count < cloud.capacity / 4) {
        cloud.capacity = points.count > cloud.capacity
                             ? std::max(points.count, cloud.capacity + cloud.capacity / 2)
                             : points.count;
        cloud.order = nullptr;
        cloud.packed = nullptr;
        device::Device& device = store.GetDevice();
        cloud.bvh.Allocate(device, m_builder,
                           (cloud.capacity + m_pointsPerBox - 1) / m_pointsPerBox, "point boxes");
        if (cloud.capacity > 0) {
            if (m_pointsPerBox > 1) {
                cloud.order = device.CreateBuffer(
                    {.bytes = uint64_t(cloud.capacity) * sizeof(uint32_t), .label = "point order"});
                cloud.packed =
                    device.CreateBuffer({.bytes = uint64_t(cloud.capacity) * 4 * sizeof(float),
                                         .label = "packed points"});
            }
        }
    }
    if (points.count != cloud.count) {
        cloud.count = points.count;
        cloud.bvh.fresh = true;
    }
    cloud.bvh.Bound(store);
    cloud.update = points.count > 0;
    PointsRecord record{};
    if (cloud.bvh.blas && points.count > 0) {
        record.blas = cloud.bvh.blas->Handle();
        record.points = points.address;
        record.widths = widths.address;
        record.pointCount = points.count;
        record.widthCount = widths.count;
        record.normals = normals.address;
        record.normalCount = normals.count;
        record.ids = ids.address;
        record.idCount = ids.count;
        record.order = cloud.order ? cloud.order->Address() : 0;
        record.packed = cloud.packed ? cloud.packed->Address() : 0;
        record.perBox = m_pointsPerBox;
        record.bounds = cloud.bvh.bounds;
    }
    const Prims& prims = m_structure.GetPrims();
    if (m_records.Set(cloud.row, record) && prims.Alive(cloud.prim)) {
        m_relinked.push_back(prims.Record(cloud.prim).world);
    }
}

void PointsTable::Run(const ChangeSet& changes, TableVersions& versions) {
    CHRONO_BATCH_RENDER_ZONE("Points/Run");
    const Prims& prims = m_structure.GetPrims();
    TrackPrims(
        changes, prims, SchemaId::Points, m_clouds, [&](uint32_t prim) { Place(prim); },
        [&](uint32_t prim) { Forget(prim); });
    if (!changes.resync) {
        const auto relink = [&](ColumnRef column, const auto& owners) {
            for (const uint32_t row : changes.ArrayRows(column)) {
                const auto owner = owners.find(row);
                if (owner != owners.end()) {
                    Cloud& cloud = m_clouds.at(owner->second);
                    if (!cloud.update) {
                        Link(cloud);
                    }
                }
            }
        };
        relink(kPointBasedPoints, m_byPointBasedRow);
        relink(kPointBasedNormals, m_byPointBasedRow);
        relink(kPointsWidths, m_byRow);
        relink(kPointsIds, m_byRow);
    }
    std::vector<uint32_t> updated;
    Build(updated);
    PublishRecords(m_records, m_structure.GetColumnStore(), SchemaId::Points, m_relinked, updated,
                   versions, TableId::Points, TableId::PointsMoved);
}

void PointsTable::Build(std::vector<uint32_t>& updated) {
    std::vector<PointsJob> jobs;
    std::vector<PointsJob> sorts;
    std::vector<Ref<bvh::Blas>> built;
    std::vector<Ref<bvh::Blas>> refit;
    uint32_t total = 0;
    uint32_t keys = 0;
    uint32_t active = 0;
    const Prims& prims = m_structure.GetPrims();
    for (auto& [prim, cloud] : m_clouds) {
        if (!cloud.update) {
            continue;
        }
        cloud.update = false;
        PointsJob job{};
        job.record = m_records.Row(cloud.row);
        job.boxes.address = cloud.bvh.boxes->Address();
        job.bounds.address = cloud.bvh.bounds.address;
        if (cloud.bvh.Rebuilds(m_maxRefits)) {
            built.push_back(cloud.bvh.blas);
            if (cloud.order) {
                PointsJob sort = job;
                sort.first = keys;
                sort.capacity = cloud.capacity;
                sorts.push_back(sort);
                keys += cloud.capacity;
            }
        } else {
            refit.push_back(cloud.bvh.blas);
        }
        job.first = total;
        job.capacity = (cloud.capacity + m_pointsPerBox - 1) / m_pointsPerBox;
        jobs.push_back(job);
        total += job.capacity;
        active += cloud.count;
        if (prims.Alive(prim)) {
            updated.push_back(prims.Record(prim).world);
        }
    }
    m_stats.clouds = uint32_t(m_clouds.size());
    m_stats.built = uint32_t(built.size());
    m_stats.refit = uint32_t(refit.size());
    m_stats.sorted = uint32_t(sorts.size());
    m_stats.capacity = total;
    m_stats.points = active;
    if (jobs.empty()) {
        return;
    }
    ColumnStore& store = m_structure.GetColumnStore();
    device::Device& device = store.GetDevice();
    store.Flush();
    const Ref<device::Buffer> table =
        device.CreateBuffer({.label = "points jobs"}, std::span<const PointsJob>(jobs));
    const uint64_t extentWords = uint64_t(jobs.size()) * 3;
    const Ref<device::Buffer> extents = device.CreateBuffer(
        {.bytes = extentWords * 2 * sizeof(uint32_t), .label = "points extents"});
    PointsParams params{};
    params.jobs.address = table->Address();
    params.extents.address = extents->Address();
    params.jobCount = uint32_t(jobs.size());
    params.total = total;
    static const profiler::ZoneId boxesZone = profiler::ZoneId::Named("Points/Boxes");
    static const profiler::ZoneId blasZone = profiler::ZoneId::Named("Points/Blas");
    device::Queue& queue = device.GetQueue();
    device::CommandEncoder encoder = queue.CreateEncoder();
    if (!sorts.empty()) {
        static const profiler::ZoneId sortZone = profiler::ZoneId::Named("Points/Sort");
        if (keys > m_sortCapacity) {
            m_sortCapacity = std::bit_ceil(keys);
            m_sort = std::make_unique<algorithms::RadixSort>(device, m_sortCapacity, 8);
            m_keys = device.CreateBuffer(
                {.bytes = uint64_t(m_sortCapacity) * 2 * sizeof(uint32_t), .label = "point keys"});
            m_values = device.CreateBuffer(
                {.bytes = uint64_t(m_sortCapacity) * sizeof(uint32_t), .label = "point values"});
        }
        const Ref<device::Buffer> sortTable =
            device.CreateBuffer({.label = "points sorts"}, std::span<const PointsJob>(sorts));
        const uint64_t sortWords = uint64_t(sorts.size()) * 3;
        const Ref<device::Buffer> sortExtents = device.CreateBuffer(
            {.bytes = sortWords * 2 * sizeof(uint32_t), .label = "points sort extents"});
        PointsParams sortParams{};
        sortParams.jobs.address = sortTable->Address();
        sortParams.extents.address = sortExtents->Address();
        sortParams.keys.address = m_keys->Address();
        sortParams.values.address = m_values->Address();
        sortParams.jobCount = uint32_t(sorts.size());
        sortParams.total = keys;
        const device::Size3 groups{.x = device::GroupCount(keys, kPointsGroup)};
        encoder.BeginZone(sortZone);
        encoder.Fill(sortExtents->Slice(0, sortWords * sizeof(uint32_t)), ~0u);
        encoder.Fill(sortExtents->Slice(sortWords * sizeof(uint32_t)), 0u);
        encoder.Dispatch(Kernels().Entry("ReducePointExtents"), sortParams, groups);
        encoder.Dispatch(Kernels().Entry("FinishPointBounds"), sortParams,
                         {.x = device::GroupCount(sortParams.jobCount, kPointsGroup)});
        encoder.Dispatch(Kernels().Entry("WritePointKeys"), sortParams, groups);
        m_sort->Sort(encoder, m_keys->Slice(0, uint64_t(keys) * 2 * sizeof(uint32_t)),
                     m_values->Slice(0, uint64_t(keys) * sizeof(uint32_t)), keys,
                     32 + std::bit_width(sortParams.jobCount - 1));
        encoder.Dispatch(Kernels().Entry("StorePointOrder"), sortParams, groups);
        encoder.EndZone();
    }
    encoder.BeginZone(boxesZone);
    encoder.Fill(extents->Slice(0, extentWords * sizeof(uint32_t)), ~0u);
    encoder.Fill(extents->Slice(extentWords * sizeof(uint32_t)), 0u);
    encoder.Dispatch(Kernels().Entry("WritePointBoxes"), params,
                     {.x = device::GroupCount(total, kPointsGroup)});
    encoder.Dispatch(Kernels().Entry("FinishPointBounds"), params,
                     {.x = device::GroupCount(params.jobCount, kPointsGroup)});
    encoder.EndZone();
    encoder.BeginZone(blasZone);
    m_builder.Build(encoder, built);
    m_builder.Refit(encoder, refit);
    encoder.EndZone();
    queue.Submit(encoder.Finish());
}

void PointsTable::Hold(std::vector<Ref<const RefCounted>>& held) const {
    HoldBuffers(held, {m_records.Buffer(), m_keys, m_values});
    for (const auto& [prim, cloud] : m_clouds) {
        cloud.bvh.Hold(held);
        if (cloud.bvh.blas) {
            if (cloud.order) {
                held.emplace_back(cloud.order);
                held.emplace_back(cloud.packed);
            }
        }
    }
}

void PointsTable::Prepare() {
    Kernels();
}

device::Program& PointsTable::Kernels() {
    if (!m_program) {
        m_program = m_structure.GetColumnStore().GetDevice().CreateProgram(
            {.modules = {"chrono_batch_render.scene.tables.kernels.points"},
             .library = &kSceneKernels,
             .entries = {"ReducePointExtents", "WritePointKeys", "StorePointOrder",
                         "WritePointBoxes", "FinishPointBounds"},
             .label = "points"});
    }
    return *m_program;
}

}
