// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/prims.h"
#include "chrono_batch_render/scene/records/transforms.h"

namespace chrono::batch_render {

CHRONO_BATCH_RENDER_ENUM(SubsetMap){
    Materials,
    Parts,
};

struct SubsetJob {
    Ptr<int> indices;
    uint subset;
    uint count;
    uint firstIndex;
    uint firstEntry;
    uint faceCount;
    uint map;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SubsetJob, 32);

struct SubsetOutput {
    Ptr<uint> values;
    uint firstEntry;
    uint count;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SubsetOutput, 16);

struct SubsetParams {
    Ptr<AuthoredData> scene;
    Ptr<SubsetJob> jobs;
    Ptr<SubsetOutput> outputs;
    Ptr<uint> ranks;
    uint jobCount;
    uint outputCount;
    uint indexCount;
    uint entryCount;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SubsetParams, 48);

static const uint kSubsetGroup = 64;

}
