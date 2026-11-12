// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/prims.h"
#include "chrono_batch_render/scene/records/sensors.h"
#include "chrono_batch_render/scene/records/transforms.h"

namespace chrono::batch_render {

struct SensorParams {
    Ptr<AuthoredData> scene;
    Ptr<CameraRecord> cameras;
    Ptr<DistortionRecord> distortions;
    Ptr<DetectorRecord> detectors;
    Ptr<LidarRecord> lidars;
    Ptr<RadarRecord> radars;
    uint cameraRows;
    uint distortionRows;
    uint detectorRows;
    uint lidarRows;
    uint radarRows;
    uint padding0;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SensorParams, 72);

static const uint kSensorGroup = 64;
static const float kSensorMillimeters = 0.001f;

}
