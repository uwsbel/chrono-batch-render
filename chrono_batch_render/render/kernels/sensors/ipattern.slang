// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.sensors.isensor;

using namespace chrono::batch_render;

interface IPattern {
    static This Load(SensorContext context);

    uint SampleCount();

    SensorRay Generate(CapturePixel pixel, uint sample);
}
