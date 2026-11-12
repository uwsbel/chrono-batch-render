// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.render.context;
__exported import chrono_batch_render.render.kernels.sensors.frame;
__exported import chrono_batch_render.render.kernels.trace.ray;

using namespace chrono::batch_render;

struct SensorRay {
    Ray ray;
    float weight;
    float spread;
}
