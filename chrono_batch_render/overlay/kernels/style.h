// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

CHRONO_BATCH_RENDER_ENUM(OverlayDepth){
    Test,
    XRay,
    Always,
};

CHRONO_BATCH_RENDER_ENUM(MarkerShape){
    Circle,
    Ring,
    Square,
    Diamond,
    Cross,
    Plus,
};

CHRONO_BATCH_RENDER_ENUM(OverlayUnits){
    Pixels,
    Meters,
};

CHRONO_BATCH_RENDER_ENUM(OverlayPrimShape){
    Frames,
    Bounds,
    Frustums,
    Lights,
    CameraImages,
};

}
