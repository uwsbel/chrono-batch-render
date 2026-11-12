// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

struct OverlayGroupParams {
    Ptr<uint> worlds;
    Ptr<uint> itemCounts;
    Ptr<uint> keys;
    Ptr<uint> order;
    Ptr<uint> starts;
    Ptr<uint> needs;
    uint rows;
    uint worldStride;
    uint groups;
    uint need;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayGroupParams, 64);

}
