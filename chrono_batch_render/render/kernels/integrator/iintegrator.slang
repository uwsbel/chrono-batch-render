// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.render.context;
__exported import chrono_batch_render.render.kernels.spectrum.spectrum;
__exported import chrono_batch_render.render.kernels.surface.compute;
import chrono_batch_render.render.kernels.random;

using namespace chrono::batch_render;

static const float kHeadlightIrradiance = kPi;

struct PathResult<Spectrum : ISpectrum> {
    Spectrum radiance;
    float3 albedo;
    SurfacePoint point;

    static PathResult<Spectrum> Miss() {
        PathResult<Spectrum> result;
        result.radiance = Spectrum.Constant(0);
        result.albedo = float3(0);
        result.point = SurfacePoint.Miss();
        return result;
    }

    bool Hit() {
        return point.prim != kNoPrim;
    }
}

interface IIntegrator {
    associatedtype Spectrum : ISpectrum;

    static This Load(SensorContext context);

    PathResult<Spectrum> Trace(Ray ray, float spread, inout Rng rng);
}
