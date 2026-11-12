// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

struct BlendParams {
    Ptr<uint> color;
    Ptr<uint> overlay;
    Ptr<uint> blended;
    uint count;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(BlendParams, 32);

}
