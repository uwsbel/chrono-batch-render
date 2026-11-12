// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/bvh/kernels/bvh.h"
#include "chrono_batch_render/scene/records/bounds.h"
#include "chrono_batch_render/scene/records/curves.h"
#include "chrono_batch_render/scene/records/meshes.h"
#include "chrono_batch_render/scene/records/points.h"
#include "chrono_batch_render/scene/records/prims.h"
#include "chrono_batch_render/scene/records/transforms.h"
#include "chrono_batch_render/scene/tables/kernels/instance_layout.h"

namespace chrono::batch_render {

struct BoundsParams {
    Ptr<AuthoredData> scene;
    Ptr<uint> origins;
    Ptr<uint> extents;
    Ptr<BvhBox> boxes;
    Ptr<InstanceLayout> instances;
    Ptr<uint> instancerExtents;
    Ptr<WorldTransform> transforms;
    Ptr<ResolvedImageable> imageables;
    Ptr<MeshRecord> meshes;
    Ptr<PointsRecord> points;
    Ptr<CurvesRecord> curves;
    Ptr<BoundableBox> primBounds;
    uint primCount;
    uint slotCount;
    uint instanceSlots;
    uint tlasCount;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(BoundsParams, 112);

static const uint kBoundsGroup = 64;

}
