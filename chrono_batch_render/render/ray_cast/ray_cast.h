// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/device/tensor.h"
#include "chrono_batch_render/export.h"
#include "chrono_batch_render/scene/prim.h"
#include "chrono_batch_render/stage/schema/ids.h"

namespace chrono::batch_render {

class Scene;

enum class RayOutput : uint32_t {
    None = 0,
    Distance = 1u << 0,
    Prim = 1u << 1,
    Normal = 1u << 2,
    Position = 1u << 3,
    InstanceId = 1u << 4,
};

constexpr RayOutput operator|(RayOutput a, RayOutput b) {
    return RayOutput(uint32_t(a) | uint32_t(b));
}

constexpr bool operator&(RayOutput a, RayOutput b) {
    return (uint32_t(a) & uint32_t(b)) != 0;
}

enum class RayCastMode : uint8_t { Closest, Any };

struct CHRONO_BATCH_RENDER_API Rays {
    device::Tensor origins;
    device::Tensor directions;
    device::Tensor tMins;
    device::Tensor tMaxes;
    device::Tensor worlds;
    float minDistance = 0;
    float maxDistance = std::numeric_limits<float>::infinity();
    uint32_t world = kGlobalWorld;
    std::optional<Prim> space;
};

struct CHRONO_BATCH_RENDER_API RayHits {
    device::Tensor hit;
    device::Tensor distance;
    device::Tensor prim;
    device::Tensor normal;
    device::Tensor position;
    device::Tensor instance;
    device::Operation done;
};

struct CHRONO_BATCH_RENDER_API RayCastDesc {
    RayCastMode mode = RayCastMode::Closest;
    RayOutput outputs = RayOutput::Distance | RayOutput::Prim;
    std::string label = "RayCast";
    std::vector<Prim> ignore;
};

class CHRONO_BATCH_RENDER_API RayCaster {
  public:
    explicit RayCaster(Ref<Scene> scene);
    ~RayCaster();
    RayCaster(RayCaster&&) noexcept;
    RayCaster& operator=(RayCaster&&) noexcept;

    device::Operation Cast(const Rays& rays, RayHits& hits, const RayCastDesc& desc = {},
                           Stream stream = Stream::None());
    RayHits Cast(const Rays& rays, const RayCastDesc& desc = {});
    Scene& GetScene() const;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
