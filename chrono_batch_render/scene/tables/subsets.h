// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/scene/records/meshes.h"
#include "chrono_batch_render/scene/tables/storage.h"
#include "chrono_batch_render/scene/tables/table.h"
#include "chrono_batch_render/stage/structure/structure.h"

namespace chrono::batch_render {

struct SubsetStats {
    uint32_t meshes = 0;
    uint32_t subsets = 0;
    bool rebuilt = false;
};

class SubsetTable : public RenderTable {
  public:
    explicit SubsetTable(Structure& structure) : m_structure(structure) {}

    std::string_view Name() const override {
        return "subsets";
    }

    std::span<const ColumnRef> Inputs() const override;
    void Describe(RenderTables& tables) const override;
    void Prepare() override;
    void Run(const ChangeSet& changes, TableVersions& versions) override;

    void Hold(std::vector<Ref<const RefCounted>>& held) const override {
        HoldBuffers(held, {m_records.Buffer()});
    }

    const SubsetStats& Stats() const {
        return m_stats;
    }

    const SubsetRecord& Record(uint32_t meshRow) const {
        return m_records.Row(meshRow);
    }

  private:
    device::Program& Kernels();
    void Rebuild();

    Structure& m_structure;
    Ref<device::Program> m_program;
    RecordTable<SubsetRecord> m_records{"subset records"};
    std::set<uint32_t> m_subsets;
    std::unordered_map<uint32_t, SubsetRecord> m_written;
    uint64_t m_seen = 0;
    bool m_built = false;
    SubsetStats m_stats;
};

}
