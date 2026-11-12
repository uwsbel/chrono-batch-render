// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.background.ibackground;

using namespace chrono::batch_render;

struct GradientBackground : IBackground {
    float3 horizon;
    float3 zenith;

    __init(float3 horizon, float3 zenith) {
        this.horizon = horizon;
        this.zenith = zenith;
    }

    static GradientBackground Load(ProductRecord* product) {
        return GradientBackground(product.backgroundLow.Vector(), product.backgroundHigh.Vector());
    }

    float3 Radiance(float3 direction) {
        return lerp(horizon, zenith, saturate(direction.z));
    }
}
