// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/bvh/kernels/bvh.h"

namespace chrono::batch_render {

static const uint kMaskInstancesThreads = 64;

struct MaskInstancesParams {
    Ptr<InstanceRecord> source;
    Ptr<InstanceRecord> destination;
    Ptr<uint> offsets;
    Ptr<uint> counts;
    uint total;
    uint worlds;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(MaskInstancesParams, 40);

static const uint kGatherWorldsThreads = 64;

struct GatherWorldsParams {
    Ptr<InstanceRecord> source;
    Ptr<InstanceRecord> destination;
    Ptr<uint> sourceOffsets;
    Ptr<uint> offsets;
    Ptr<uint> worlds;
    Ptr<uint> counts;
    Ptr<uint> gatheredCounts;
    uint total;
    uint selected;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(GatherWorldsParams, 64);

}
