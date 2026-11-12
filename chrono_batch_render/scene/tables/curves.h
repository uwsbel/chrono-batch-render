// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "chrono_batch_render/algorithms/scan.h"
#include "chrono_batch_render/bvh/builder.h"
#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/scene/records/curves.h"
#include "chrono_batch_render/scene/tables/procedural.h"
#include "chrono_batch_render/scene/tables/storage.h"
#include "chrono_batch_render/scene/tables/table.h"
#include "chrono_batch_render/stage/structure/structure.h"

namespace chrono::batch_render {

struct CurvesStats {
    uint32_t prims = 0;
    uint32_t built = 0;
    uint32_t refit = 0;
    uint32_t points = 0;
    uint32_t curves = 0;
    uint32_t segments = 0;
    uint32_t capacity = 0;
    uint32_t readBacks = 0;
};

inline constexpr const char* kShapeOptionsFile =
    "chrono_batch_render/render/kernels/shapes/options.slang";

class CurvesTable : public RenderTable {
  public:
    CurvesTable(Structure& structure, bvh::Builder& builder, uint32_t maxRefits = 16,
                float tolerance = kDefaultCurveTolerance, uint32_t maxSteps = kDefaultMaxCurveSteps,
                bool exact = false)
        : m_structure(structure), m_builder(builder), m_maxRefits(maxRefits),
          m_tolerance(tolerance), m_maxSteps(std::clamp(maxSteps, 1u, kMostCurveSteps)),
          m_exact(exact) {}

    ~CurvesTable() override;

    std::string_view Name() const override {
        return "curves";
    }

    std::span<const ColumnRef> Inputs() const override;
    std::span<const TableId> TableOutputs() const override;
    void Describe(RenderTables& tables) const override;
    void Prepare() override;
    void Run(const ChangeSet& changes, TableVersions& versions) override;
    void Hold(std::vector<Ref<const RefCounted>>& held) const override;

    bool Pending() const override {
        return !m_pending.empty() && m_pending.front().download.Ready();
    }

    bool Exact() const {
        return m_exact;
    }

    std::string ShapeOptions() const;

    const CurvesStats& Stats() const {
        return m_stats;
    }

    const CurvesRecord& Record(uint32_t curvesRow) const {
        return m_records.Row(curvesRow);
    }

    std::vector<uint32_t> SpanSteps(uint32_t curvesRow) const;

  private:
    struct Prim {
        uint32_t prim = kNoPrim;
        uint32_t generation = 0;
        uint32_t pointBasedRow = kNoRow;
        uint32_t curvesRow = kNoRow;
        uint32_t row = kNoRow;
        uint32_t basisRow = kNoRow;
        uint32_t kindRow = kNoRow;
        uint32_t kind = kCurveKindBasis;
        uint32_t count = 0;
        uint32_t curveCount = 0;
        uint32_t pointCapacity = 0;
        uint32_t curveCapacity = 0;
        uint32_t wanted = 0;
        ProceduralBlas bvh;
        Ref<device::Buffer> segments;
        Ref<device::Buffer> spanFirst;
        Ref<device::Buffer> spanSteps;
        Ref<device::Buffer> starts;
        Ref<device::Buffer> varyingStarts;
        Ref<device::Buffer> knotStarts;
        bool drawn = false;
        bool update = false;
    };

    void Place(uint32_t prim);
    void Forget(uint32_t prim);
    void Link(Prim& curves);
    void Resize(Prim& curves, uint32_t segments);
    void WriteRecord(Prim& curves);
    void EnsureScratch(uint32_t items);
    void Collect();
    void Build(std::vector<uint32_t>& updated);
    device::Program& Kernels();

    Structure& m_structure;
    bvh::Builder& m_builder;
    uint32_t m_maxRefits;
    float m_tolerance;
    uint32_t m_maxSteps;
    bool m_exact;
    uint64_t m_seen = 0;
    std::unique_ptr<algorithms::Scan> m_scan;
    uint32_t m_scanCapacity = 0;
    Ref<device::Buffer> m_counts;
    Ref<device::Buffer> m_scanned;
    Ref<device::Buffer> m_varying;
    Ref<device::Buffer> m_scannedVarying;
    Ref<device::Buffer> m_steps;
    Ref<device::Buffer> m_stepFirst;
    Ref<device::Buffer> m_orders;
    Ref<device::Buffer> m_scannedOrders;
    Ref<device::Program> m_program;
    std::unordered_map<uint32_t, Prim> m_prims;
    std::unordered_map<uint32_t, uint32_t> m_byPointBasedRow;
    std::unordered_map<uint32_t, uint32_t> m_byCurvesRow;
    std::unordered_map<uint32_t, uint32_t> m_byHermiteRow;
    std::unordered_map<uint32_t, uint32_t> m_byNurbsRow;
    RecordTable<CurvesRecord> m_records{"curves records"};
    std::vector<uint32_t> m_relinked;

    struct PendingTotals {
        device::Download download;
        Ref<device::Buffer> totals;
        std::vector<std::pair<uint32_t, uint32_t>> prims;
        std::vector<bool> restepped;
    };

    std::deque<PendingTotals> m_pending;
    CurvesStats m_stats;
};

}
