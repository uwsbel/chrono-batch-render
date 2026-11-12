// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/scene/records/views.h"
#include "chrono_batch_render/scene/tables/products.h"
#include "chrono_batch_render/scene/tables/table.h"
#include "chrono_batch_render/stage/structure/structure.h"

namespace chrono::batch_render {

class ViewTable : public RenderTable {
  public:
    ViewTable(Structure& structure, const RenderTables& tables, const ProductTable& products)
        : m_structure(structure), m_tables(tables), m_products(products) {}

    std::string_view Name() const override {
        return "views";
    }

    std::span<const ColumnRef> Inputs() const override {
        return {};
    }

    std::span<const TableId> TableInputs() const override {
        static constexpr TableId kTables[] = {TableId::Cameras, TableId::Products};
        return kTables;
    }

    void Prepare() override;
    void Run(const ChangeSet& changes, TableVersions& versions) override;
    void Describe(RenderTables& tables) const override;

    void Hold(std::vector<Ref<const RefCounted>>& held) const override {
        HoldBuffers(held, {m_views, m_starts});
    }

    const Ref<device::Buffer>& Views() const {
        return m_views;
    }

    const Ref<device::Buffer>& Starts() const {
        return m_starts;
    }

    uint32_t Count() const {
        return m_count;
    }

  private:
    device::Program& Kernels();

    Structure& m_structure;
    const RenderTables& m_tables;
    const ProductTable& m_products;
    Ref<device::Program> m_program;
    Ref<device::Buffer> m_views;
    Ref<device::Buffer> m_starts;
    uint32_t m_count = 0;
};

}
