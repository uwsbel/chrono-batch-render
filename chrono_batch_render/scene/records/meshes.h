// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

struct MeshRecord {
    uint64_t blas;
    ArrayRef corners;
    ArrayRef faces;
    ArrayRef starts;
    ArrayRef adjacency;
    ArrayRef normals;
    ArrayRef bounds;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(MeshRecord, 104);

struct SubsetRecord {
    ArrayRef materials;
    ArrayRef parts;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SubsetRecord, 32);

}
