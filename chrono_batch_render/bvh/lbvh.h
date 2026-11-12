// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "chrono_batch_render/algorithms/radix_sort.h"
#include "chrono_batch_render/bvh/builder.h"
#include "chrono_batch_render/bvh/kernels/lbvh.h"
#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/ref.h"

namespace chrono::batch_render::device {
class Device;
}

namespace chrono::batch_render::bvh {

LbvhGeometry LbvhEntry(device::Program& program, const char* what, const Geometry& geometry);

class LbvhBuilder {
  public:
    static constexpr uint32_t kMaxPrimitives = 65535 * 256;

    LbvhBuilder(device::Device& device, uint32_t capacity, uint32_t segments = 1);

    uint32_t Capacity() const {
        return m_capacity;
    }

    uint32_t Segments() const {
        return m_segments;
    }

    void Build(device::CommandEncoder& encoder, std::span<const Geometry> geometries,
               std::span<const device::BufferRange> outputs) const;

    void Build(device::CommandEncoder& encoder, device::BufferRange instances,
               std::span<const uint32_t> capacities, std::optional<device::BufferRange> counts,
               std::span<const device::BufferRange> outputs) const;

  private:
    void Record(device::CommandEncoder& encoder, LbvhParams params,
                std::span<const uint32_t> capacities, std::span<const device::BufferRange> outputs,
                std::span<const LbvhGeometry> geometries) const;

    device::BufferRange Words(uint64_t at, uint64_t words) const;

    uint32_t m_capacity;
    uint32_t m_segments;
    Ref<device::Program> m_program;
    device::Kernel* m_prepare;
    device::Kernel* m_morton;
    device::Kernel* m_hierarchy;
    device::Kernel* m_fitBoxes;
    device::Kernel* m_seed;
    device::Kernel* m_advance;
    device::Kernel* m_collapse;
    device::Kernel* m_finish;
    algorithms::RadixSort m_sort;
    Ref<device::Buffer> m_scratch;
    Ref<device::Buffer> m_layout;
    Ref<device::Buffer> m_state;
    LbvhParams m_params{};
};

}
