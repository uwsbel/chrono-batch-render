// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

#ifndef __SLANG__
#include "chrono_batch_render/scene/records/instances.h"
#include "chrono_batch_render/scene/records/render_tables.h"
#endif

namespace chrono::batch_render {

struct RayCastParams {
    Ptr<RenderTables> tables;
    Ptr<float> origins;
    Ptr<float> directions;
    Ptr<float> tMins;
    Ptr<float> tMaxes;
    Ptr<uint> worlds;
    Ptr<uint> hits;
    Ptr<float> distances;
    Ptr<uint> prims;
    Ptr<float> normals;
    Ptr<float> positions;
    Ptr<int64_t> instances;
    Ptr<uint> ignored;
    float minDistance;
    float maxDistance;
    uint world;
    uint count;
    uint ignoredCount;
    uint spaceRow;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(RayCastParams, 128);

static const uint kRayCastGroup = 64;

}
