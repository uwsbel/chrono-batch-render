// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/views.h"
#include "chrono_batch_render/stage/schema/ids.h"

namespace chrono::batch_render {

static const uint kViewGroup = 64;

struct ViewEntry {
    uint product;
    uint camera;
    uint distortion;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(ViewEntry, 16);

struct ViewParams {
    Ptr<CameraRecord> cameras;
    Ptr<DistortionRecord> distortions;
    Ptr<ProductRecord> products;
    Ptr<ViewEntry> entries;
    Ptr<ViewRecord> views;
    uint count;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(ViewParams, 48);

}
