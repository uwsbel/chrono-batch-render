// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

static const uint kScanThreads = 256;
static const uint kScanItems = 4;
static const uint kScanTileItems = kScanThreads * kScanItems;

struct ScanParams {
    Ptr<uint> input;
    Ptr<uint> output;
    Ptr<uint> sums;
    Ptr<uint> deviceCount;
    uint count;
    uint hasSums;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(ScanParams, 40);

}
