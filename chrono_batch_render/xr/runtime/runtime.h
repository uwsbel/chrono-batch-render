// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/device/tensor.h"
#include "chrono_batch_render/xr/session.h"

namespace chrono::batch_render::xr {

double HostSeconds();

struct FrameClock {
    double poses = 0;
    double display = 0;
    double period = 0;
    uint32_t missed = 0;
    bool estimated = false;
};

struct EyeImages {
    device::Tensor color;
    device::Tensor depth;
    device::Operation ready;
};

class Runtime {
  public:
    virtual ~Runtime() = default;

    virtual void ConfigureDevice(device::DeviceDesc& desc) = 0;
    virtual void Start(device::Device& device) = 0;
    virtual void Stop() = 0;
    virtual bool Running() = 0;
    virtual void BeginFrame(Frame& frame, FrameClock& clock) = 0;
    virtual void EndFrame(const Frame& frame, const EyeImages* eyes) = 0;
    virtual bool DepthLayer() const = 0;
    virtual BlendMode Blend() const = 0;
    virtual void RequestExit() = 0;
    virtual bool SetRefreshRate(float hz) = 0;
    virtual Uint2 Recommended() const = 0;

    virtual bool RelocateViews(Frame&) {
        return false;
    }

    virtual Uint2 Presented(Uint2 scaled) const {
        return scaled;
    }

    virtual uint32_t Features() const = 0;
    virtual void FillStatus(SessionStatus& status) const = 0;

    virtual RuntimeMetrics Metrics(const SessionStats&) {
        return {};
    }

    virtual device::Tensor Presented() const {
        return {};
    }

    virtual device::Tensor PresentedDepth() const {
        return {};
    }
};

std::unique_ptr<Runtime> CreateSimulatedRuntime(const SessionDesc& desc);
std::unique_ptr<Runtime> CreateOpenXrRuntime(const SessionDesc& desc);

}
