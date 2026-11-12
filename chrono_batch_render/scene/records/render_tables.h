// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/bounds.h"
#include "chrono_batch_render/scene/records/curves.h"
#include "chrono_batch_render/scene/records/instances.h"
#include "chrono_batch_render/scene/records/lights.h"
#include "chrono_batch_render/scene/records/materials.h"
#include "chrono_batch_render/scene/records/meshes.h"
#include "chrono_batch_render/scene/records/points.h"
#include "chrono_batch_render/scene/records/prims.h"
#include "chrono_batch_render/scene/records/products.h"
#include "chrono_batch_render/scene/records/semantics.h"
#include "chrono_batch_render/scene/records/sensors.h"
#include "chrono_batch_render/scene/records/spectra.h"
#include "chrono_batch_render/scene/records/tangents.h"
#include "chrono_batch_render/scene/records/textures.h"
#include "chrono_batch_render/scene/records/transforms.h"
#include "chrono_batch_render/scene/records/views.h"
#include "chrono_batch_render/stage/schema/ids.h"
#include "chrono_batch_render/stage/schema/schema.h"

namespace chrono::batch_render {

struct RenderTables {
    PrimTable prims;
    Ptr<WorldTransform> transforms;
    Ptr<ResolvedImageable> imageables;
    Ptr<MotionRecord> motions;
    Ptr<MeshRecord> meshes;
    Ptr<SubsetRecord> subsets;
    Ptr<PointsRecord> points;
    Ptr<CurvesRecord> curves;
    Ptr<TangentRecord> tangents;
    Ptr<SurfaceRecord> surfaces;
    Ptr<BoundableBox> primBounds;
    Ptr<TextureRecord> textures;
    Ptr<uint64_t> textureBindings;
    Ptr<InstanceLayout> instances;
    Ptr<LightLayout> lights;
    Ptr<MinimalLightRecord> minimalLights;
    Ptr<CollectionRecord> collections;
    Ptr<LightShapeRecord> lightShapes;
    Ptr<RenderOrigin> origins;
    Ptr<WorldTransform> slotWorlds;
    Ptr<CameraRecord> cameras;
    Ptr<DistortionRecord> distortions;
    Ptr<DetectorRecord> detectors;
    Ptr<LidarRecord> lidars;
    Ptr<RadarRecord> radars;
    Ptr<ProductRecord> products;
    Ptr<ViewRecord> views;
    Ptr<uint> viewStarts;
    Ptr<MaterialRecord> materials;
    Ptr<uint> materialWords;
    Ptr<CheckerRecord> checkers;
    SpectraRecord spectra;
    Ptr<float> temperatures;
    Ptr<RadioRecord> radios;
    uint temperaturePrimvar;
    uint padding0;
    SemanticsData semantics;
    uint worldCount;
    uint padding;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(RenderTables, 432);

}
