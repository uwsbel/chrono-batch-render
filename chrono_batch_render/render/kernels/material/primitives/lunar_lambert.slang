// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.device.kernels.device;
__exported import chrono_batch_render.render.kernels.material.primitives.photometric;

struct LunarLambertBsdf<Spectrum : ISpectrum> : IDifferentiable {
    Spectrum albedo;
    no_diff float3 coefficients;

    [Differentiable]
    __init(Spectrum albedo, no_diff float3 coefficients) {
        this.albedo = albedo;
        this.coefficients = coefficients;
    }

    [Differentiable]
    Spectrum Estimate() {
        return albedo;
    }

    [Differentiable]
    Spectrum Evaluate(no_diff float3 outgoing, no_diff float3 incoming) {
        const PhotometricAngles s = no_diff PhotometricAngles(outgoing, incoming);
        const float g = degrees(no_diff s.Phase());
        const float3 a = coefficients;
        const float l = 1 + g * (a.x + g * (a.y + g * a.z));
        return albedo.Scale(max(2 * l / (s.mu0 + s.mu) + (1 - l), 0.0f) / kPi);
    }

    float Pdf(float3 incoming) {
        return PhotometricPdf(incoming);
    }

    float3 Direction(float2 u) {
        return CosineHemisphere(u);
    }
}
