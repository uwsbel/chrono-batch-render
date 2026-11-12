// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/scene/records/render_tables.h"

namespace chrono::batch_render {

static const uint kPipelineGroup = 64;
static const uint kMostSignalChannels = 8;
static const uint kSignalHalf = 1;
static const uint kSignalMosaic = 2;
static const uint kSignalSampled = 4;
static const uint kSignalRecords = 16;
static const uint kSignalWorld = 32;
static const uint kSignalStrideShift = 8;
static const uint kRecordWords = 14;
static const uint kStatWords = 16;
static const uint kReduceGroups = 64;

struct SensorPipelineValue {
    uint64_t address;
    uint channels;
    uint flags;
    uint64_t returned;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SensorPipelineValue, 24);

struct SensorPipelineCapture {
    uint64_t firstElement;
    Ptr<uint> words;
    uint sensor;
    uint detector;
    uint width;
    uint height;
    uint capture;
    uint identity;
    uint firstValue;
    uint camera;
    Ptr<float> state;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SensorPipelineCapture, 56);

struct SensorPipelineParams {
    Ptr<RenderTables> tables;
    Ptr<SensorPipelineCapture> captures;
    Ptr<SensorPipelineValue> values;
    uint64_t elementCount;
    uint captureCount;
    uint padding0;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(SensorPipelineParams, 40);

}
