// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/stage/views/staged.h"

#include <algorithm>

namespace chrono::batch_render {

namespace {

bool InHostMemory(DLDevice device) {
    return device.device_type == kDLCPU || device.device_type == kDLCUDAHost ||
           device.device_type == kDLROCMHost || device.device_type == kDLCUDAManaged;
}

std::optional<uint64_t> ContiguousBytes(const DLTensor& source) {
    uint64_t elements = 1;
    for (int axis = source.ndim; axis-- > 0;) {
        const int64_t side = source.shape[axis];
        if (side < 0) {
            return std::nullopt;
        }
        if (source.strides != nullptr && side > 1 && source.strides[axis] != int64_t(elements)) {
            return std::nullopt;
        }
        elements *= uint64_t(side);
    }
    const uint64_t bits = uint64_t(source.dtype.bits) * std::max<uint16_t>(source.dtype.lanes, 1);
    if (bits % 8 != 0) {
        return std::nullopt;
    }
    return elements * (bits / 8);
}

}

std::optional<StagedTensor> StagedTensor::Stage(device::Device& device, const DLTensor& source,
                                                Stream producer) {
    const std::optional<uint64_t> bytes = ContiguousBytes(source);
    if (!bytes) {
        return std::nullopt;
    }
    StagedTensor staged;
    staged.m_shape.assign(source.shape, source.shape + source.ndim);
    staged.m_dtype = source.dtype;
    const auto* data = static_cast<const std::byte*>(source.data) + source.byte_offset;
    if (InHostMemory(source.device)) {
        if (producer.IsCompute()) {
            return std::nullopt;
        }
        staged.m_host.assign(data, data + *bytes);
        return staged;
    }
    const std::optional<DLDevice> shared = device.SharedDevice();
    if (!producer.IsCompute() || !shared || shared->device_type != source.device.device_type) {
        return std::nullopt;
    }
    staged.m_buffer = device.StageOnStream(uint64_t(data), *bytes, producer, "deferred write");
    const device::ExternalAddress external = staged.m_buffer->External(source.device.device_type);
    staged.m_address = external.address;
    staged.m_device = external.device;
    staged.m_producer = producer;
    return staged;
}

DLTensor StagedTensor::View() const {
    DLTensor view{};
    view.data =
        m_buffer ? reinterpret_cast<void*>(m_address) : const_cast<std::byte*>(m_host.data());
    view.device = m_device;
    view.ndim = int32_t(m_shape.size());
    view.dtype = m_dtype;
    view.shape = const_cast<int64_t*>(m_shape.data());
    return view;
}

}
