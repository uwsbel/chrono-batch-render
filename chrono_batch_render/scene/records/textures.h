// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

static const uint kCheckerTextureBinding = 0;
static const uint kNoTextures = 0;

struct TextureRecord {
    uint textures;
    uint shaping;
    uint width;
    uint height;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(TextureRecord, 16);

}
