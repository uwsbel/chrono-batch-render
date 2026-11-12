// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/kernels/shared.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/ref.h"

namespace chrono::batch_render {
struct ScanParams;
}

namespace chrono::batch_render::device {
class Device;
}

namespace chrono::batch_render::algorithms {

class Scan {
  public:
    static constexpr uint32_t kMaxItems = 1u << 30;

    Scan(device::Device& device, uint32_t capacity);

    Scan(const Scan&) = delete;
    Scan& operator=(const Scan&) = delete;
    Scan(Scan&&) noexcept = default;
    Scan& operator=(Scan&&) noexcept = default;

    void ExclusiveSum(device::CommandEncoder& encoder, device::BufferRange input,
                      device::BufferRange output, uint32_t count) const;

    void ExclusiveSum(device::CommandEncoder& encoder, device::BufferRange input,
                      device::BufferRange output, device::BufferRange count,
                      uint32_t capacity) const;

  private:
    void Record(device::CommandEncoder& encoder, device::BufferRange input,
                device::BufferRange output, uint32_t count, Ptr<uint32_t> liveCount) const;

    void RecordLevel(device::CommandEncoder& encoder, ScanParams params, size_t level) const;

    uint32_t m_capacity;
    Ref<device::Program> m_program;
    device::Kernel* m_sumTiles;
    device::Kernel* m_scanTiles;
    Ref<device::Buffer> m_sums;
    std::vector<uint64_t> m_levelOffsets;
    Ref<device::Buffer> m_unlimitedCount;
};

}
