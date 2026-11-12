// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <dlpack/dlpack.h>

#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/device/stream.h"

namespace chrono::batch_render {

class StagedTensor {
  public:
    static std::optional<StagedTensor> Stage(device::Device& device, const DLTensor& source,
                                             Stream producer);

    DLTensor View() const;

    Stream Producer() const {
        return m_producer;
    }

  private:
    std::vector<int64_t> m_shape;
    std::vector<std::byte> m_host;
    Ref<device::Buffer> m_buffer;
    DLDataType m_dtype{};
    DLDevice m_device{kDLCPU, 0};
    uint64_t m_address = 0;
    Stream m_producer = Stream::None();
};

inline bool StageOptional(device::Device& device, const DLTensor* tensor, Stream producer,
                          std::optional<StagedTensor>& staged) {
    if (tensor == nullptr) {
        return true;
    }
    staged = StagedTensor::Stage(device, *tensor, producer);
    return staged.has_value();
}

inline const DLTensor* ViewOptional(const std::optional<StagedTensor>& staged, DLTensor& view) {
    if (!staged) {
        return nullptr;
    }
    view = staged->View();
    return &view;
}

}
