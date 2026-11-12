// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/columns/kernels/paged.h"
#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

struct PackEntry {
    uint64_t pages;
    uint row;
    uint words;
    uint destination;
    uint value;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PackEntry, 24);

struct PackParams {
    Ptr<PackEntry> entries;
    Ptr<uint> words;
    uint count;
    uint padding0;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PackParams, 24);

static const uint kPackGroup = 64;

}
