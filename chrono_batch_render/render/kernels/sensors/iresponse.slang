// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.sensors.ipattern;
import chrono_batch_render.render.kernels.random;

using namespace chrono::batch_render;

interface IResponse<P : IPattern> {
    static This Load(SensorContext context, P pattern);

    [mutating]
    void Add(SensorRay ray, inout Rng rng);

    void Write(SensorContext context, P pattern, CapturePixel pixel);
}
