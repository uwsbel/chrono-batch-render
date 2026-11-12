// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/columns/kernels/authored.h"

namespace chrono::batch_render {

struct StampTarget {
    Ptr<uint64_t> stamps;
    Ptr<uint64_t> tablePrims;
    Ptr<AuthoredData> scene;
    uint64_t stamp;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(StampTarget, 32);

struct StampParams {
    StampTarget target;
    Ptr<uint> rows;
    Ptr<uint64_t> mask;
    uint count;
    uint firstRow;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(StampParams, 56);

struct CompactParams {
    Ptr<uint64_t> stamps;
    Ptr<uint64_t> tablePrims;
    Ptr<uint> out;
    uint64_t since;
    uint64_t until;
    uint rows;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(CompactParams, 48);

struct WorldsParams {
    Ptr<AuthoredData> scene;
    Ptr<uint> out;
    uint64_t since;
    uint64_t until;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(WorldsParams, 32);

static const uint kChangesGroup = 64;
static const uint kChangedRowsOffset = 4;

#ifdef __SLANG__

uint ViewRow(Ptr<uint> rows, uint firstRow, uint item) {
    return rows == nullptr ? firstRow + item : rows[item];
}

bool Selected(Ptr<uint64_t> mask, uint item) {
    return mask == nullptr || ((mask[item >> 6] >> (item & 63)) & 1) != 0;
}

void StampRow(StampTarget target, uint row) {
    if (target.stamps != nullptr) {
        Paged<uint64_t> stamps = {target.stamps};
        stamps[row] = target.stamp;
    }
    if (target.tablePrims == nullptr) {
        return;
    }
    const Paged<uint> prims = {target.tablePrims};
    const uint prim = prims[row];
    if (prim == kNoPrim) {
        return;
    }
    AuthoredData* scene = target.scene;
    scene->worldStamps[WorldSlot(scene->prims.world[prim])] = target.stamp;
}

#endif

}
