// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

static const uint kOverlayNoImage = ~0u;

struct OverlayFill {
    uint64_t pixels;
    float x[3];
    float y[3];
    float depth[3];
    uint width;
    uint height;
    uint rowStride;
    uint opaque;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayFill, 64);

struct OverlayCameraImage {
    uint64_t pixels;
    uint camera;
    uint width;
    uint height;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayCameraImage, 24);

}
