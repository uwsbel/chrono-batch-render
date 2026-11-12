// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/device/tensor.h"
#include "chrono_batch_render/stage/views/view.h"

namespace chrono::batch_render {

class ViewState;

class WorldPoseSource {
  public:
    virtual ~WorldPoseSource() = default;

    virtual device::Tensor Read(ViewState& view, device::DType type, Stream consumer,
                                QuaternionOrder order) = 0;

    virtual device::Tensor ReadScales(ViewState& view, device::DType type, Stream consumer) = 0;

    virtual device::Tensor Localize(ViewState& view, const device::Tensor& values,
                                    const device::Tensor* rotations, QuaternionOrder order,
                                    const device::Tensor& sources) = 0;
};

}
