// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/bvh/kernels/lbvh.h"

namespace chrono::batch_render {

static const uint kRefitThreads = 64;

static const uint kRefitNoParent = 0xFFFFFFFF;

struct RefitParams {
    Ptr<uint64_t> headers;
    Ptr<InstanceRecord> instances;
    Ptr<uint> instanceOffsets;
    Ptr<LbvhGeometry> geometries;
    Ptr<uint> nodeOffsets;
    Ptr<uint> parents;
    Ptr<uint> arrivals;
    Ptr<float> boxes;
    Ptr<float> leafBoxes;
    uint segments;
    uint nodes;
    uint kind;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(RefitParams, 88);

}
