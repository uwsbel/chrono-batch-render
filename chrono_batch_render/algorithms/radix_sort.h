// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <optional>

#include "chrono_batch_render/algorithms/scan.h"
#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/kernels/shared.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/ref.h"

namespace chrono::batch_render::device {
class Device;
}

namespace chrono::batch_render::algorithms {

class RadixSort {
  public:
    static constexpr uint32_t kMaxItems = Scan::kMaxItems;

    RadixSort(device::Device& device, uint32_t capacity, uint32_t keyBytes = 8);

    void Sort(device::CommandEncoder& encoder, device::BufferRange keys, device::BufferRange values,
              uint32_t count, std::optional<uint32_t> keyBits = std::nullopt) const;

    void Sort(device::CommandEncoder& encoder, device::BufferRange keys, device::BufferRange values,
              device::BufferRange count, uint32_t capacity,
              std::optional<uint32_t> keyBits = std::nullopt) const;

  private:
    void Record(device::CommandEncoder& encoder, device::BufferRange keys,
                device::BufferRange values, uint32_t count, Ptr<uint32_t> liveCount,
                std::optional<uint32_t> keyBits) const;

    uint32_t m_capacity;
    uint32_t m_keyWords;
    Ref<device::Program> m_program;
    device::Kernel* m_countDigits;
    device::Kernel* m_scatter;
    Scan m_scan;
    Ref<device::Buffer> m_keys;
    Ref<device::Buffer> m_values;
    Ref<device::Buffer> m_counts;
    Ref<device::Buffer> m_unlimitedCount;
};

}
