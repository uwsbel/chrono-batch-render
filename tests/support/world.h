// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/scene_impl.h"

namespace chrono::batch_render::test {

struct WorldPlace {
    Double3 position;
    Quatd rotation;

    Double3 Axis(Double3 local) const {
        return Rotate(rotation, local);
    }
};

inline std::vector<WorldPlace> WorldPlaces(Scene& scene, const std::vector<std::string>& paths) {
    const std::vector<double> words =
        scene.Select(paths).ReadWorldPoses(device::DType::Float64).ToHost<double>();
    std::vector<WorldPlace> places;
    for (size_t at = 0; at + 7 <= words.size(); at += 7) {
        places.push_back({{words[at], words[at + 1], words[at + 2]},
                          {words[at + 3], words[at + 4], words[at + 5], words[at + 6]}});
    }
    return places;
}

inline WorldPlace LocateWorldPlace(Scene& scene, const std::string& path) {
    return WorldPlaces(scene, {path}).front();
}

inline std::vector<WorldTransform> HierarchyTransforms(Scene& scene,
                                                       const std::vector<std::string>& paths) {
    SceneImpl& impl = SceneImpl::Get(scene);
    const auto lock = impl.Lock();
    impl.UpdateIfPending();
    std::vector<uint32_t> prims;
    for (const std::string& path : paths) {
        const std::optional<Prim> prim = scene.Find(path);
        prims.push_back(prim ? prim->Index() : kNoPrim);
    }
    const std::vector<uint32_t> rows =
        impl.GetStructure().GetPrims().Rows(prims, SchemaId::Xformable);
    return impl.GetColumnStore().ReadRows<WorldTransform>(impl.GetTransforms().Transforms(), rows);
}

}
