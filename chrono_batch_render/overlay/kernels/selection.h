// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

#ifndef __SLANG__
#include "chrono_batch_render/scene/records/render_tables.h"
#endif

namespace chrono::batch_render {

struct OverlayUniteParams {
    Ptr<RenderTables> tables;
    Ptr<uint> prims;
    Ptr<uint> groups;
    Ptr<uint> boxes;
    uint primCount;
    uint primStride;
    uint groupStride;
    uint boxCount;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OverlayUniteParams, 48);

}
