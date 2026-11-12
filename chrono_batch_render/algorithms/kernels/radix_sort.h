// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

static const uint kDigitBits = 8;
static const uint kDigitBins = 1 << kDigitBits;
static const uint kSortThreads = kDigitBins;
static const uint kSortItems = 4;
static const uint kSortTileItems = kSortThreads * kSortItems;

struct SortParams {
    Ptr<uint> keys;
    Ptr<uint> values;
    Ptr<uint> keysOut;
    Ptr<uint> valuesOut;
    Ptr<uint> counts;
    Ptr<uint> deviceCount;
    uint count;
    uint shift;
    uint mask;
    uint tiles;
    uint keyWords;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SortParams, 72);

}
