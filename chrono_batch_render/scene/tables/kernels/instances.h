// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/bvh/kernels/bvh.h"
#include "chrono_batch_render/scene/records/curves.h"
#include "chrono_batch_render/scene/records/instances.h"
#include "chrono_batch_render/scene/records/meshes.h"
#include "chrono_batch_render/scene/records/points.h"
#include "chrono_batch_render/scene/records/prims.h"
#include "chrono_batch_render/scene/records/transforms.h"
#include "chrono_batch_render/scene/tables/kernels/instance_layout.h"

namespace chrono::batch_render {

struct InstanceParams {
    Ptr<AuthoredData> scene;
    Ptr<InstanceLayout> layout;
    Ptr<InstanceRecord> instances;
    Ptr<ShapeRecord> shapes;
    Ptr<uint> tlases;
    Ptr<SlotHit> hits;
    Ptr<RenderOrigin> origins;
    Ptr<WorldTransform> transforms;
    Ptr<ResolvedImageable> imageables;
    Ptr<MeshRecord> meshes;
    Ptr<PointsRecord> points;
    Ptr<CurvesRecord> curves;
    Ptr<WorldTransform> slotWorlds;
    uint64_t unitBox;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(InstanceParams, 112);

struct OriginParams {
    Ptr<AuthoredData> scene;
    Ptr<InstanceLayout> layout;
    Ptr<uint> products;
    Ptr<RenderOrigin> origins;
    Ptr<uint> viewers;
    Ptr<WorldTransform> transforms;
    uint productCount;
    uint tlasCount;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(OriginParams, 56);

static const uint kInstanceGroup = 64;
static const uint kViewerGroups = 32;

}
