// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

struct TangentRecord {
    ArrayRef tangents;
    uint primvar;
    uint padding0;
    uint padding1;
    uint padding2;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(TangentRecord, 32);

}
