// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.device.kernels.device;
__exported import chrono_batch_render.render.kernels.material.primitives.photometric;

struct OrenNayarBsdf<Spectrum : ISpectrum> : IDifferentiable {
    Spectrum albedo;
    float sigma;

    [Differentiable]
    __init(Spectrum albedo, float sigma) {
        this.albedo = albedo;
        this.sigma = sigma;
    }

    [Differentiable]
    Spectrum Estimate() {
        return albedo;
    }

    [Differentiable]
    Spectrum Evaluate(no_diff float3 outgoing, no_diff float3 incoming) {
        const PhotometricAngles s = no_diff PhotometricAngles(outgoing, incoming);
        const float sigma2 = sigma * sigma;
        const float a = 1 - 0.5f * sigma2 / (sigma2 + 0.33f);
        const float b = 0.45f * sigma2 / (sigma2 + 0.09f);
        const float i = acos(clamp(s.mu0, -1.0f, 1.0f));
        const float e = acos(clamp(s.mu, -1.0f, 1.0f));
        const float facets = max(0.0f, s.cosPsi) * sin(max(i, e)) * tan(min(i, e));
        return albedo.Scale((a + b * facets) / kPi);
    }

    float Pdf(float3 incoming) {
        return PhotometricPdf(incoming);
    }

    float3 Direction(float2 u) {
        return CosineHemisphere(u);
    }
}
