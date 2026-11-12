// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/scene/tables/curves.h"

#include <algorithm>
#include <bit>
#include <optional>

#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/scene/kernels.h"
#include "chrono_batch_render/scene/records/render_tables.h"
#include "chrono_batch_render/scene/schema/columns.h"
#include "chrono_batch_render/scene/tables/kernels/curves.h"
#include "chrono_batch_render/stage/publish/change_set.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {

namespace {

constexpr ColumnRef kLayout[] = {kCurvesWidthsInterpolation, kBasisCurvesType, kBasisCurvesBasis,
                                 kBasisCurvesWrap, kPointBasedNormalsInterpolation};

constexpr ColumnRef kInputs[] = {
    kPointBasedPoints,          kCurvesCurveVertexCounts, kCurvesWidths,
    kCurvesWidthsInterpolation, kBasisCurvesType,         kBasisCurvesBasis,
    kBasisCurvesWrap,           kPointBasedNormals,       kPointBasedNormalsInterpolation,
    kHermiteCurvesTangents,     kNurbsCurvesOrder,        kNurbsCurvesKnots,
    kNurbsCurvesPointWeights};

constexpr TableId kTableOutputs[] = {TableId::Curves, TableId::CurvesMoved};

constexpr uint32_t kNanWord = 0x7FC00000u;

uint32_t Grown(uint32_t count, uint32_t capacity) {
    if (count > capacity) {
        return std::max(count, capacity + capacity / 2);
    }
    return count < capacity / 4 ? count : capacity;
}

Ref<device::Buffer> Words(device::Device& device, uint32_t count, const char* label) {
    return count > 0
               ? device.CreateBuffer({.bytes = uint64_t(count) * sizeof(uint32_t), .label = label})
               : nullptr;
}

}

CurvesTable::~CurvesTable() {
    for (const PendingTotals& pending : m_pending) {
        pending.download.Wait();
    }
}

std::span<const ColumnRef> CurvesTable::Inputs() const {
    return kInputs;
}

std::span<const TableId> CurvesTable::TableOutputs() const {
    return kTableOutputs;
}

void CurvesTable::Describe(RenderTables& tables) const {
    tables.curves.address = m_records.Buffer() ? m_records.Buffer()->Address() : 0;
}

void CurvesTable::Place(uint32_t prim) {
    const Prims& prims = m_structure.GetPrims();
    Prim curves;
    curves.prim = prim;
    curves.generation = prims.Generation(prim);
    curves.pointBasedRow = prims.Row(prim, SchemaId::PointBased);
    curves.curvesRow = prims.Row(prim, SchemaId::Curves);
    curves.row = curves.curvesRow;
    const SchemaId schema = prims.Record(prim).schema;
    if (IsA(schema, SchemaId::BasisCurves)) {
        curves.basisRow = prims.Row(prim, SchemaId::BasisCurves);
    } else if (IsA(schema, SchemaId::HermiteCurves)) {
        curves.kind = kCurveKindHermite;
        curves.kindRow = prims.Row(prim, SchemaId::HermiteCurves);
        m_byHermiteRow[curves.kindRow] = prim;
    } else if (IsA(schema, SchemaId::NurbsCurves)) {
        curves.kind = kCurveKindNurbs;
        curves.kindRow = prims.Row(prim, SchemaId::NurbsCurves);
        m_byNurbsRow[curves.kindRow] = prim;
    }
    m_byPointBasedRow[curves.pointBasedRow] = prim;
    m_byCurvesRow[curves.curvesRow] = prim;
    Link(m_prims[prim] = std::move(curves));
}

void CurvesTable::Forget(uint32_t prim) {
    Prim& curves = m_prims.at(prim);
    curves.bvh.Release(m_structure.GetColumnStore());
    m_records.Set(curves.row, CurvesRecord{});
    m_byPointBasedRow.erase(curves.pointBasedRow);
    m_byCurvesRow.erase(curves.curvesRow);
    if (curves.kind == kCurveKindHermite) {
        m_byHermiteRow.erase(curves.kindRow);
    } else if (curves.kind == kCurveKindNurbs) {
        m_byNurbsRow.erase(curves.kindRow);
    }
    m_prims.erase(prim);
}

void CurvesTable::Link(Prim& curves) {
    ColumnStore& store = m_structure.GetColumnStore();
    device::Device& device = store.GetDevice();
    const ArrayRef points = store.Array(kPointBasedPoints, curves.pointBasedRow);
    const ArrayRef counts = store.Array(kCurvesCurveVertexCounts, curves.curvesRow);
    const uint32_t pointCapacity = Grown(points.count, curves.pointCapacity);
    if (pointCapacity != curves.pointCapacity) {
        curves.pointCapacity = pointCapacity;
        curves.spanFirst = Words(device, pointCapacity, "curve span firsts");
        curves.spanSteps = Words(device, pointCapacity, "curve span steps");
    }
    const uint32_t curveCapacity = Grown(counts.count, curves.curveCapacity);
    if (curveCapacity != curves.curveCapacity) {
        curves.curveCapacity = curveCapacity;
        curves.starts = Words(device, curveCapacity, "curve starts");
        curves.varyingStarts = Words(device, curveCapacity, "curve varying starts");
        curves.knotStarts = curves.kind == kCurveKindNurbs
                                ? Words(device, curveCapacity, "curve knot starts")
                                : nullptr;
    }
    if (points.count != curves.count || counts.count != curves.curveCount) {
        curves.count = points.count;
        curves.curveCount = counts.count;
        curves.bvh.fresh = true;
    }
    curves.bvh.Bound(store);
    curves.drawn = points.count > 0 && counts.count > 0;
    curves.update = curves.drawn;
    WriteRecord(curves);
}

void CurvesTable::Resize(Prim& curves, uint32_t segments) {
    if (segments <= curves.bvh.capacity && segments >= curves.bvh.capacity / 4 && curves.bvh.blas) {
        return;
    }
    device::Device& device = m_structure.GetColumnStore().GetDevice();
    curves.bvh.Allocate(device, m_builder, segments > 0 ? segments + segments / 2 : 0,
                        "curve boxes");
    curves.segments =
        curves.bvh.capacity > 0
            ? device.CreateBuffer({.bytes = uint64_t(curves.bvh.capacity) * sizeof(CurveSegment),
                                   .label = "curve segments"})
            : nullptr;
    WriteRecord(curves);
}

void CurvesTable::WriteRecord(Prim& curves) {
    ColumnStore& store = m_structure.GetColumnStore();
    const ArrayRef points = store.Array(kPointBasedPoints, curves.pointBasedRow);
    const ArrayRef widths = store.Array(kCurvesWidths, curves.curvesRow);
    const ArrayRef counts = store.Array(kCurvesCurveVertexCounts, curves.curvesRow);
    const ArrayRef normals = store.Array(kPointBasedNormals, curves.pointBasedRow);
    CurvesRecord record{};
    record.kind = curves.kind;
    if (curves.drawn) {
        record.blas = curves.bvh.blas ? curves.bvh.blas->Handle() : 0;
        record.points = points.address;
        record.widths = widths.address;
        record.counts = counts.address;
        record.starts = curves.starts->Address();
        record.varyingStarts = curves.varyingStarts->Address();
        record.spanFirst = curves.spanFirst->Address();
        record.spanSteps = curves.spanSteps->Address();
        record.segments = curves.segments ? curves.segments->Address() : 0;
        record.normals = normals.address;
        record.normalCount = normals.count;
        if (curves.kind == kCurveKindHermite) {
            const ArrayRef tangents = store.Array(kHermiteCurvesTangents, curves.kindRow);
            record.tangents = tangents.address;
            record.tangentCount = tangents.count;
        } else if (curves.kind == kCurveKindNurbs) {
            const ArrayRef orders = store.Array(kNurbsCurvesOrder, curves.kindRow);
            const ArrayRef knots = store.Array(kNurbsCurvesKnots, curves.kindRow);
            const ArrayRef weights = store.Array(kNurbsCurvesPointWeights, curves.kindRow);
            record.orders = orders.count >= counts.count ? orders.address : 0;
            record.knots = knots.address;
            record.knotCount = knots.count;
            record.knotStarts = curves.knotStarts ? curves.knotStarts->Address() : 0;
            record.pointWeights = weights.address;
            record.weightCount = weights.count;
        }
        record.pointCount = points.count;
        record.widthCount = widths.count;
        record.curveCount = counts.count;
        record.segmentCount = curves.bvh.capacity;
        record.bounds = curves.bvh.bounds;
    }
    const Prims& prims = m_structure.GetPrims();
    if (m_records.Set(curves.row, record) && prims.Alive(curves.prim)) {
        m_relinked.push_back(prims.Record(curves.prim).world);
    }
}

void CurvesTable::Run(const ChangeSet& changes, TableVersions& versions) {
    CHRONO_BATCH_RENDER_ZONE("Curves/Run");
    const Prims& prims = m_structure.GetPrims();
    ColumnStore& store = m_structure.GetColumnStore();
    const uint64_t seen = store.WriteSequence();
    TrackPrims(
        changes, prims, SchemaId::Curves, m_prims, [&](uint32_t prim) { Place(prim); },
        [&](uint32_t prim) { Forget(prim); });
    if (!changes.resync) {
        const auto relink = [&](ColumnRef column, const auto& owners, bool layout) {
            for (const uint32_t row : changes.ArrayRows(column)) {
                const auto owner = owners.find(row);
                if (owner != owners.end()) {
                    Prim& curves = m_prims.at(owner->second);
                    if (!curves.update) {
                        Link(curves);
                    }
                    curves.bvh.fresh = curves.bvh.fresh || layout;
                }
            }
        };
        relink(kPointBasedPoints, m_byPointBasedRow, false);
        relink(kCurvesWidths, m_byCurvesRow, false);
        relink(kPointBasedNormals, m_byPointBasedRow, false);
        relink(kHermiteCurvesTangents, m_byHermiteRow, false);
        relink(kNurbsCurvesPointWeights, m_byNurbsRow, false);
        relink(kNurbsCurvesOrder, m_byNurbsRow, true);
        relink(kNurbsCurvesKnots, m_byNurbsRow, true);
        relink(kCurvesCurveVertexCounts, m_byCurvesRow, true);
        if (std::ranges::any_of(
                kLayout, [&](ColumnRef column) { return store.LastWrite(column) > m_seen; })) {
            for (auto& [prim, curves] : m_prims) {
                if (!curves.update) {
                    Link(curves);
                }
                curves.bvh.fresh = true;
            }
        }
    }
    m_seen = seen;
    Collect();
    std::vector<uint32_t> updated;
    Build(updated);
    PublishRecords(m_records, store, SchemaId::Curves, m_relinked, updated, versions,
                   TableId::Curves, TableId::CurvesMoved);
}

void CurvesTable::Collect() {
    const Prims& prims = m_structure.GetPrims();
    while (!m_pending.empty() && m_pending.front().download.Ready()) {
        const PendingTotals& pending = m_pending.front();
        const Span<const std::byte> bytes = pending.download.Bytes();
        const uint32_t* wanted = reinterpret_cast<const uint32_t*>(bytes.data());
        for (size_t i = 0; i < pending.prims.size(); ++i) {
            const auto [prim, generation] = pending.prims[i];
            const auto found = m_prims.find(prim);
            if (found == m_prims.end() || prims.Generation(prim) != generation ||
                !pending.restepped[i]) {
                continue;
            }
            Prim& curves = found->second;
            const uint32_t segments = wanted[i * 2];
            curves.wanted = segments;
            if (curves.drawn &&
                (segments > curves.bvh.capacity || segments < curves.bvh.capacity / 4)) {
                if (!curves.update) {
                    Link(curves);
                }
                Resize(curves, segments);
                curves.bvh.refits = m_maxRefits;
            }
        }
        m_pending.pop_front();
    }
}

std::string CurvesTable::ShapeOptions() const {
    const bool curves = m_structure.GetColumnStore().Schema(SchemaId::Curves).Rows() > 0;
    return std::string("static const bool kCurves = ") + (curves ? "true" : "false") +
           ";\nstatic const bool kExactCurves = " + (m_exact ? "true" : "false") + ";\n";
}

std::vector<uint32_t> CurvesTable::SpanSteps(uint32_t curvesRow) const {
    const CurvesRecord& record = m_records.Row(curvesRow);
    std::vector<uint32_t> steps(record.pointCount);
    for (const auto& [prim, curves] : m_prims) {
        if (curves.row == curvesRow && curves.spanSteps && !steps.empty()) {
            curves.spanSteps->GetDevice().GetQueue().Read(
                curves.spanSteps->Slice(0, steps.size() * sizeof(uint32_t)), steps.data());
        }
    }
    return steps;
}

void CurvesTable::EnsureScratch(uint32_t items) {
    if (items <= m_scanCapacity) {
        return;
    }
    device::Device& device = m_structure.GetColumnStore().GetDevice();
    m_scanCapacity = std::bit_ceil(items);
    m_scan = std::make_unique<algorithms::Scan>(device, m_scanCapacity);
    m_counts = Words(device, m_scanCapacity, "curve counts");
    m_scanned = Words(device, m_scanCapacity, "curve scanned counts");
    m_varying = Words(device, m_scanCapacity, "curve varying counts");
    m_scannedVarying = Words(device, m_scanCapacity, "curve scanned varying counts");
    m_steps = Words(device, m_scanCapacity, "curve steps");
    m_orders = Words(device, m_scanCapacity, "curve orders");
    m_scannedOrders = Words(device, m_scanCapacity, "curve scanned orders");
    m_stepFirst = Words(device, m_scanCapacity, "curve step firsts");
}

void CurvesTable::Build(std::vector<uint32_t>& updated) {
    std::vector<CurvesJob> jobs;
    std::vector<Prim*> owners;
    bool reading = false;
    uint32_t total = 0;
    uint32_t countTotal = 0;
    uint32_t active = 0;
    const Prims& prims = m_structure.GetPrims();
    for (auto& [prim, curves] : m_prims) {
        if (!curves.update) {
            continue;
        }
        curves.update = false;
        CurvesJob job{};
        job.first = total;
        job.countFirst = countTotal;
        job.curvesRow = curves.curvesRow;
        job.basisRow = curves.basisRow;
        job.pointBasedRow = curves.pointBasedRow;
        job.restep =
            curves.bvh.fresh || !curves.bvh.blas || curves.bvh.refits >= m_maxRefits ? 1 : 0;
        reading = reading || !curves.bvh.blas;
        jobs.push_back(job);
        owners.push_back(&curves);
        total += curves.count;
        countTotal += curves.curveCount;
        active += curves.count;
        if (prims.Alive(prim)) {
            updated.push_back(prims.Record(prim).world);
        }
    }
    m_stats = {};
    m_stats.prims = uint32_t(m_prims.size());
    m_stats.points = active;
    m_stats.curves = countTotal;
    if (jobs.empty()) {
        return;
    }
    ColumnStore& store = m_structure.GetColumnStore();
    device::Device& device = store.GetDevice();
    device::Queue& queue = device.GetQueue();
    store.Flush();
    EnsureScratch(std::max(total, countTotal));
    const auto describe = [&] {
        for (size_t i = 0; i < jobs.size(); ++i) {
            const Prim& curves = *owners[i];
            jobs[i].record = m_records.Row(curves.row);
            jobs[i].boxes.address = curves.bvh.boxes ? curves.bvh.boxes->Address() : 0;
            jobs[i].bounds.address = curves.bvh.bounds.address;
        }
        return device.CreateBuffer({.label = "curves jobs"}, std::span<const CurvesJob>(jobs));
    };
    Ref<device::Buffer> table = describe();
    const Ref<device::Buffer> totals = Words(device, uint32_t(jobs.size()) * 2, "curves totals");
    const uint64_t extentWords = uint64_t(jobs.size()) * 3;
    const Ref<device::Buffer> extents = device.CreateBuffer(
        {.bytes = extentWords * 2 * sizeof(uint32_t), .label = "curves extents"});
    CurvesParams params{};
    params.scene.address = store.DeviceData();
    params.jobs.address = table->Address();
    params.extents.address = extents->Address();
    params.counts.address = m_counts->Address();
    params.scanned.address = m_scanned->Address();
    params.varying.address = m_varying->Address();
    params.scannedVarying.address = m_scannedVarying->Address();
    params.orders.address = m_orders->Address();
    params.scannedOrders.address = m_scannedOrders->Address();
    params.steps.address = m_steps->Address();
    params.stepFirst.address = m_stepFirst->Address();
    params.totals.address = totals->Address();
    params.jobCount = uint32_t(jobs.size());
    params.total = total;
    params.countTotal = countTotal;
    params.maxSteps = m_maxSteps;
    params.tolerance = m_tolerance;
    params.exact = m_exact ? 1 : 0;
    const auto words = [](const Ref<device::Buffer>& buffer, uint32_t count) {
        return buffer->Slice(0, uint64_t(count) * sizeof(uint32_t));
    };
    const device::Size3 curveGroups{.x = device::GroupCount(countTotal, kCurvesGroup)};
    const device::Size3 spanGroups{.x = device::GroupCount(total, kCurvesGroup)};
    static const profiler::ZoneId stepsZone = profiler::ZoneId::Named("Curves/Steps");
    static const profiler::ZoneId segmentsZone = profiler::ZoneId::Named("Curves/Segments");
    static const profiler::ZoneId blasZone = profiler::ZoneId::Named("Curves/Blas");

    std::optional<device::CommandEncoder> encoder(queue.CreateEncoder());
    const bool restepping =
        std::ranges::any_of(jobs, [](const CurvesJob& job) { return job.restep != 0; });
    encoder->BeginZone(stepsZone);
    if (restepping) {
        encoder->Fill(totals->Slice(0), 0u);
        encoder->Dispatch(Kernels().Entry("GatherCurveCounts"), params, curveGroups);
        m_scan->ExclusiveSum(*encoder, words(m_counts, countTotal), words(m_scanned, countTotal),
                             countTotal);
        m_scan->ExclusiveSum(*encoder, words(m_varying, countTotal),
                             words(m_scannedVarying, countTotal), countTotal);
        m_scan->ExclusiveSum(*encoder, words(m_orders, countTotal),
                             words(m_scannedOrders, countTotal), countTotal);
        encoder->Dispatch(Kernels().Entry("WriteCurveStarts"), params, curveGroups);
        encoder->Dispatch(Kernels().Entry("CountCurveSteps"), params, spanGroups);
        m_scan->ExclusiveSum(*encoder, words(m_steps, total), words(m_stepFirst, total), total);
        encoder->Dispatch(Kernels().Entry("SumCurveSteps"), params,
                          {.x = device::GroupCount(params.jobCount, kCurvesGroup)});
    }
    encoder->EndZone();
    if (reading) {
        queue.Submit(encoder->Finish());
        std::vector<uint32_t> wanted(jobs.size() * 2);
        queue.Read(totals->Slice(0), wanted.data());
        ++m_stats.readBacks;
        for (size_t i = 0; i < jobs.size(); ++i) {
            Prim& curves = *owners[i];
            if (!curves.bvh.blas) {
                curves.wanted = wanted[i * 2];
                Resize(curves, wanted[i * 2]);
            }
        }
        table = describe();
        params.jobs.address = table->Address();
        encoder.emplace(queue.CreateEncoder());
    }

    std::vector<Ref<bvh::Blas>> built;
    std::vector<Ref<bvh::Blas>> refit;
    encoder->BeginZone(segmentsZone);
    if (restepping) {
        encoder->Dispatch(Kernels().Entry("FitCurveSteps"), params, spanGroups);
        m_scan->ExclusiveSum(*encoder, words(m_steps, total), words(m_stepFirst, total), total);
        encoder->Dispatch(Kernels().Entry("StoreCurveSteps"), params, spanGroups);
    }
    for (size_t i = 0; i < jobs.size(); ++i) {
        Prim& curves = *owners[i];
        if (!curves.bvh.blas) {
            continue;
        }
        if (curves.bvh.Rebuilds(m_maxRefits) || jobs[i].restep != 0) {
            encoder->Fill(curves.bvh.boxes->Slice(0), kNanWord);
            encoder->Fill(curves.segments->Slice(0), ~0u);
            built.push_back(curves.bvh.blas);
        } else {
            refit.push_back(curves.bvh.blas);
        }
        m_stats.capacity += curves.bvh.capacity;
        m_stats.segments += std::min(curves.wanted, curves.bvh.capacity);
    }
    encoder->Fill(extents->Slice(0, extentWords * sizeof(uint32_t)), ~0u);
    encoder->Fill(extents->Slice(extentWords * sizeof(uint32_t)), 0u);
    encoder->Dispatch(Kernels().Entry("WriteCurveSegments"), params, spanGroups);
    encoder->Dispatch(Kernels().Entry("FinishCurveBounds"), params,
                      {.x = device::GroupCount(params.jobCount, kCurvesGroup)});
    encoder->EndZone();
    encoder->BeginZone(blasZone);
    m_builder.Build(*encoder, built);
    m_builder.Refit(*encoder, refit);
    encoder->EndZone();
    queue.Submit(encoder->Finish());
    if (restepping) {
        PendingTotals pending{.download = queue.ReadAsync(totals->Slice(0)), .totals = totals};
        for (size_t i = 0; i < jobs.size(); ++i) {
            pending.prims.emplace_back(owners[i]->prim, owners[i]->generation);
            pending.restepped.push_back(jobs[i].restep != 0);
        }
        m_pending.push_back(std::move(pending));
    }
    m_stats.built = uint32_t(built.size());
    m_stats.refit = uint32_t(refit.size());
}

void CurvesTable::Hold(std::vector<Ref<const RefCounted>>& held) const {
    HoldBuffers(held, {m_records.Buffer(), m_counts, m_scanned, m_varying, m_scannedVarying,
                       m_steps, m_stepFirst, m_orders, m_scannedOrders});
    for (const auto& [prim, curves] : m_prims) {
        curves.bvh.Hold(held);
        for (const Ref<device::Buffer>& buffer :
             {curves.segments, curves.spanFirst, curves.spanSteps, curves.starts,
              curves.varyingStarts, curves.knotStarts}) {
            if (buffer) {
                held.emplace_back(buffer);
            }
        }
    }
}

void CurvesTable::Prepare() {
    Kernels();
}

device::Program& CurvesTable::Kernels() {
    if (!m_program) {
        m_program = m_structure.GetColumnStore().GetDevice().CreateProgram(
            {.modules = {"chrono_batch_render.scene.tables.kernels.curves"},
             .library = &kSceneKernels,
             .entries = {"GatherCurveCounts", "WriteCurveStarts", "CountCurveSteps",
                         "SumCurveSteps", "FitCurveSteps", "StoreCurveSteps", "WriteCurveSegments",
                         "FinishCurveBounds"},
             .label = "curves"});
    }
    return *m_program;
}

}
