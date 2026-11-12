// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/scene/scene.h"

namespace chrono::batch_render::xr {

struct RunDesc {
    bool simulated = false;
    device::DeviceDesc device;
    std::string camera;
    uint32_t frames = 0;
    bool profile = false;
    std::filesystem::path record;
    std::filesystem::path replay;
    std::function<Ref<Scene>(device::Device&)> scene;
};

using RunXr = int (*)(const RunDesc* desc, std::string* error);

inline constexpr const char* kRunXr = "ChronoBatchRenderRunXr";

}
