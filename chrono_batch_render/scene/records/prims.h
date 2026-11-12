// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/columns/kernels/authored.h"
#include "chrono_batch_render/stage/columns/kernels/paged.h"

namespace chrono::batch_render {

struct PrimTable {
    Paged<uint> schema;
    Paged<uint> world;
    Paged<uint> flags;
    Paged<uint> slots;
    Paged<uint64_t> apis;
    Paged<uint> rows;
    Paged<uint> parent;
    uint count;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PrimTable, 64);

}
