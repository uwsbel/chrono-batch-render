// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <optional>

#include "chrono_batch_render/scene/rotation.h"

namespace chrono::batch_render::app {

struct OrbitCamera {
    Double3 target;
    double yaw = 0;
    double pitch = 0;
    double distance = 5;

    static constexpr double kMaxPitch = 1.55;

    Double3 Eye() const;
    Quatd Orient() const;
    void Orbit(double deltaYaw, double deltaPitch);
    void Pan(double right, double ahead);
    void PanVertical(double right, double raised);
    void Zoom(double steps);
    void LookFrom(const Double3& eye, const Quatd& orient, const std::optional<Double3>& focus);
};

}
