// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/scene/records/spectra.h"
#include "chrono_batch_render/scene/tables/table.h"
#include "chrono_batch_render/stage/structure/structure.h"

namespace chrono::batch_render {

class SpectralTable : public RenderTable {
  public:
    explicit SpectralTable(Structure& structure) : m_structure(structure) {}

    std::string_view Name() const override {
        return "spectra";
    }

    std::span<const ColumnRef> Inputs() const override;
    void Run(const ChangeSet& changes, TableVersions& versions) override;
    void Describe(RenderTables& tables) const override;

    void Hold(std::vector<Ref<const RefCounted>>& held) const override {
        HoldBuffers(held, {m_rgb, m_bands, m_ranges, m_samples, m_sigmoids, m_sigmoidScales});
    }

  private:
    Structure& m_structure;
    Ref<device::Buffer> m_rgb;
    Ref<device::Buffer> m_bands;
    Ref<device::Buffer> m_ranges;
    Ref<device::Buffer> m_samples;
    Ref<device::Buffer> m_sigmoids;
    Ref<device::Buffer> m_sigmoidScales;
    uint32_t m_count = 0;
    uint32_t m_detectors = 0;
};

}
