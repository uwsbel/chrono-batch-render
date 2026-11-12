// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

struct OverlayOutline {
    Ptr<uint> bits;
    uint bitWords;
    uint color;
    float width;
    uint overlay;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayOutline, 24);

}
