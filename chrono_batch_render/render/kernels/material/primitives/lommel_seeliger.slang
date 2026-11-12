// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.device.kernels.device;
__exported import chrono_batch_render.render.kernels.material.primitives.photometric;

struct LommelSeeligerBsdf<Spectrum : ISpectrum> : IDifferentiable {
    Spectrum albedo;
    no_diff float xi;

    [Differentiable]
    __init(Spectrum albedo, no_diff float xi) {
        this.albedo = albedo;
        this.xi = xi;
    }

    [Differentiable]
    Spectrum Estimate(no_diff float3 outgoing) {
        const float mu = max(outgoing.z, 1e-3f);
        return albedo.Scale(0.5f * (1 - mu * log((1 + mu) / mu)));
    }

    [Differentiable]
    Spectrum Evaluate(no_diff float3 outgoing, no_diff float3 incoming) {
        const PhotometricAngles s = no_diff PhotometricAngles(outgoing, incoming);
        const float phase = no_diff HenyeyGreenstein(xi, s.cosG);
        return albedo.Scale(phase / (4 * kPi * (s.mu0 + s.mu)));
    }

    float Pdf(float3 incoming) {
        return PhotometricPdf(incoming);
    }

    float3 Direction(float2 u) {
        return CosineHemisphere(u);
    }
}
