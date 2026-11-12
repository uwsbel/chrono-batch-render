// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

static const uint kOverlayNoPick = ~0u;

struct OverlayPickQuery {
    uint view;
    float x;
    float y;
    float radius;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayPickQuery, 16);

struct OverlayPickResult {
    uint key;
    uint work;
    uint batch;
    uint row;
    uint piece;
    uint id;
    uint overlay;
    uint world;
    float distance;
    float depth;
    uint hit;
    uint padding;
    float position[3];
    float origin[3];
    float direction[3];
    uint prim;
    uint tail[2];
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayPickResult, 96);

}
