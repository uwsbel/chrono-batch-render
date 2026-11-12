// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.query.hit;

#include "chrono_batch_render/render/kernels/render/render.h"

using namespace chrono::batch_render;

struct SensorContext {
    RenderParams params;
    Capture* capture;
    HitQuery query;

    __init(RenderParams params, Capture* capture) {
        this.params = params;
        this.capture = capture;
        query = HitQuery(params.tables, params.tables->prims.world[capture->plan.sensor]);
    }
}
