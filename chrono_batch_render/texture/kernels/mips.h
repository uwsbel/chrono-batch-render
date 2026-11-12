// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"
#include "chrono_batch_render/device/kernels/texture.h"

namespace chrono::batch_render {

static const uint kMipGroup = 64;

struct MipJob {
    Ptr<uint> texels;
    uint source;
    uint target;
    uint sourceWidth;
    uint sourceHeight;
    uint width;
    uint height;
    uint first;
    TexelFormat format;
    TexelColorSpace colorSpace;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(MipJob, 48);

struct MipParams {
    Ptr<MipJob> jobs;
    uint jobCount;
    uint total;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(MipParams, 16);

}
