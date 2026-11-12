// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/render_vars.h"
#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

struct ProductRecord {
    ArrayRef sensors;
    ArrayRef vars;
    uint sensorSchema;
    uint width;
    uint height;
    uint returns;
    float frameRate;
    float startTime;
    float missDistance;
    uint renderMode;
    uint minimalMode;
    uint spectralMode;
    float textureLodBias;
    uint maxAnisotropy;
    uint maxBounces;
    uint castShadows;
    float shadowVisibility;
    uint hemispheric;
    Float3 ambient;
    uint background;
    Float3 ground;
    uint backgroundKind;
    Float3 backgroundColor;
    uint pixelSamples;
    Float3 backgroundLow;
    uint conform;
    Float3 backgroundHigh;
    float pixelAspect;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(ProductRecord, 176);

struct PlannedCapture {
    uint64_t firstPixel;
    uint product;
    uint sensor;
    uint layer;
    uint width;
    uint height;
    uint taxonomy;
    float missDistance;
    uint returns;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PlannedCapture, 40);

}
