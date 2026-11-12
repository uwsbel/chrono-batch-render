// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

struct BoundableBox {
    Float3 lower;
    Float3 upper;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(BoundableBox, 24);

#ifdef __SLANG__

uint OrderedFloat(float value) {
    const uint bits = asuint(value);
    return (bits & 0x80000000u) != 0 ? ~bits : bits | 0x80000000u;
}

float FloatFromOrdered(uint bits) {
    return asfloat((bits & 0x80000000u) != 0 ? bits & 0x7FFFFFFFu : ~bits);
}

#endif

}
