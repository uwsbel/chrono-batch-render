// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/bvh/kernels/bvh.h"
#include "chrono_batch_render/scene/records/points.h"

namespace chrono::batch_render {

struct PointsJob {
    PointsRecord record;
    Ptr<BvhBox> boxes;
    Ptr<Float3> bounds;
    uint first;
    uint capacity;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PointsJob, 120);

struct PointsParams {
    Ptr<PointsJob> jobs;
    Ptr<uint> extents;
    Ptr<uint> keys;
    Ptr<uint> values;
    uint jobCount;
    uint total;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PointsParams, 40);

static const uint kPointsGroup = 256;

}
