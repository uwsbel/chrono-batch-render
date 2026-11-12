// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/instances.h"
#include "chrono_batch_render/scene/records/meshes.h"
#include "chrono_batch_render/scene/records/prims.h"
#include "chrono_batch_render/scene/records/transforms.h"
#include "chrono_batch_render/scene/tables/kernels/instance_layout.h"
#include "chrono_batch_render/stage/schema/primvars.h"

namespace chrono::batch_render {

struct SurfaceParams {
    Ptr<AuthoredData> scene;
    Ptr<InstanceLayout> layout;
    Ptr<MeshRecord> meshes;
    Ptr<SurfaceRecord> surfaces;
    Ptr<uint> tlases;
    uint normalsName;
    uint displayOpacityName;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SurfaceParams, 48);

static const uint kSurfaceGroup = 64;

}
