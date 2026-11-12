// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/bvh/kernels/bvh.h"
#include "chrono_batch_render/scene/records/curves.h"
#include "chrono_batch_render/scene/records/prims.h"

namespace chrono::batch_render {

struct CurvesJob {
    CurvesRecord record;
    Ptr<BvhBox> boxes;
    Ptr<Float3> bounds;
    uint first;
    uint countFirst;
    uint curvesRow;
    uint basisRow;
    uint restep;
    uint pointBasedRow;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(CurvesJob, 216);

struct CurvesParams {
    Ptr<AuthoredData> scene;
    Ptr<CurvesJob> jobs;
    Ptr<uint> extents;
    Ptr<uint> counts;
    Ptr<uint> scanned;
    Ptr<uint> varying;
    Ptr<uint> scannedVarying;
    Ptr<uint> orders;
    Ptr<uint> scannedOrders;
    Ptr<uint> steps;
    Ptr<uint> stepFirst;
    Ptr<uint> totals;
    uint jobCount;
    uint total;
    uint countTotal;
    uint maxSteps;
    float tolerance;
    uint exact;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(CurvesParams, 120);

static const uint kCurvesGroup = 256;

}
