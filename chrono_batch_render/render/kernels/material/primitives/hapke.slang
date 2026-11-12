// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.device.kernels.device;
import chrono_batch_render.io.kernels.encoding;
__exported import chrono_batch_render.render.kernels.material.primitives.photometric;

using namespace chrono::batch_render;

struct HapkeRoughness {
    float mu0e;
    float mue;
    float shadowing;

    __init(PhotometricAngles s, float thetaBar) {
        if (thetaBar < 1e-4f) {
            mu0e = s.mu0;
            mue = s.mu;
            shadowing = 1;
            return;
        }
        const float tanT = tan(thetaBar);
        const float chi = 1 / sqrt(1 + kPi * tanT * tanT);
        const float i = max(acos(clamp(s.mu0, -1.0f, 1.0f)), 1e-4f);
        const float e = max(acos(clamp(s.mu, -1.0f, 1.0f)), 1e-4f);
        const float psi = min(acos(clamp(s.cosPsi, -1.0f, 1.0f)), kPi - 1e-4f);
        const float f = exp(-2 * tan(0.5f * psi));
        const float sin2HalfPsi = 0.5f * (1 - cos(psi));
        const float tanI = tan(i);
        const float tanE = tan(e);
        const float e1i = exp(-2 / (kPi * tanT * tanI));
        const float e1e = exp(-2 / (kPi * tanT * tanE));
        const float e2i = exp(-1 / (kPi * tanT * tanT * tanI * tanI));
        const float e2e = exp(-1 / (kPi * tanT * tanT * tanE * tanE));
        const float etaI = chi * (cos(i) + sin(i) * tanT * e2i / (2 - e1i));
        const float etaE = chi * (cos(e) + sin(e) * tanT * e2e / (2 - e1e));
        if (i <= e) {
            const float d = 2 - e1e - psi / kPi * e1i;
            mu0e = chi * (cos(i) + sin(i) * tanT * (cos(psi) * e2e + sin2HalfPsi * e2i) / d);
            mue = chi * (cos(e) + sin(e) * tanT * (e2e - sin2HalfPsi * e2i) / d);
            shadowing = mue / etaE * s.mu0 / etaI * chi / (1 - f + f * chi * s.mu0 / etaI);
        } else {
            const float d = 2 - e1i - psi / kPi * e1e;
            mu0e = chi * (cos(i) + sin(i) * tanT * (e2i - sin2HalfPsi * e2e) / d);
            mue = chi * (cos(e) + sin(e) * tanT * (cos(psi) * e2i + sin2HalfPsi * e2e) / d);
            shadowing = mue / etaE * s.mu0 / etaI * chi / (1 - f + f * chi * s.mu / etaE);
        }
    }
}

float HapkePorosity(float phi) {
    const float q = 1.209f * pow(clamp(phi, 1e-6f, 0.74f), 2.0f / 3.0f);
    return -log(1 - q) / q;
}

float4 HapkeParams(float b, float c, float phi, float bs0, float hs) {
    const uint opposition =
        FloatToHalf(max(bs0, 0.0f)) | (FloatToHalf(clamp(hs, 1e-3f, 1.0f)) << 16);
    return float4(b, c, HapkePorosity(phi), asfloat(opposition));
}

[Differentiable]
Spectrum HapkeDiffusiveReflectance<Spectrum : ISpectrum>(Spectrum w) {
    const Spectrum one = Spectrum.Constant(1);
    const Spectrum gamma = (one - w).Sqrt();
    return (one - gamma) / (one + gamma);
}

[Differentiable]
Spectrum HapkeH<Spectrum : ISpectrum>(Spectrum w, Spectrum r0, no_diff float x) {
    const float xs = max(x, 1e-6f);
    const float logTerm = log((1 + xs) / xs);
    const Spectrum one = Spectrum.Constant(1);
    const Spectrum inner = r0.Scale(1 - logTerm * xs) + Spectrum.Constant(0.5f * logTerm);
    return one / (one - (w * inner).Scale(xs));
}

struct HapkeTerms {
    float mu0e;
    float mue;
    float k;
    float single;
    float scale;
}

[noinline]
HapkeTerms HapkeGeometry(float3 outgoing, float3 incoming, float thetaBar, float4 params) {
    const PhotometricAngles s = PhotometricAngles(outgoing, incoming);
    const uint packed = asuint(params.w);
    const float bs0 = HalfToFloat(packed & 0xffff);
    const float hs = HalfToFloat(packed >> 16);
    const HapkeRoughness rough = HapkeRoughness(s, min(thetaBar, radians(60.0f)));
    HapkeTerms terms;
    terms.k = max(params.z, 1.0f);
    terms.mu0e = max(rough.mu0e, 1e-6f);
    terms.mue = max(rough.mue, 1e-6f);
    const float phase = DoubleHenyeyGreenstein(params.x, params.y, s.cosG);
    const float shoe = bs0 / (1 + tan(0.5f * s.Phase()) / hs);
    terms.single = phase * (1 + shoe) - 1;
    terms.scale = terms.k * terms.mu0e / (terms.mu0e + terms.mue) * rough.shadowing /
                  (4 * kPi * max(s.mu0, 1e-6f));
    return terms;
}

struct HapkeBsdf<Spectrum : ISpectrum> : IDifferentiable {
    Spectrum albedo;
    no_diff float thetaBar;
    no_diff float4 params;

    [Differentiable]
    __init(Spectrum albedo, no_diff float thetaBar, no_diff float4 params) {
        this.albedo = albedo.Clamp(0, 1);
        this.thetaBar = thetaBar;
        this.params = params;
    }

    [Differentiable]
    Spectrum Estimate() {
        return HapkeDiffusiveReflectance(albedo);
    }

    [Differentiable]
    Spectrum Evaluate(no_diff float3 outgoing, no_diff float3 incoming) {
        const HapkeTerms terms = no_diff HapkeGeometry(outgoing, incoming, thetaBar, params);
        const Spectrum r0 = HapkeDiffusiveReflectance(albedo);
        const Spectrum multiple =
            HapkeH(albedo, r0, terms.mu0e / terms.k) * HapkeH(albedo, r0, terms.mue / terms.k);
        return (albedo * (multiple + Spectrum.Constant(terms.single))).Scale(terms.scale);
    }

    float Pdf(float3 incoming) {
        return PhotometricPdf(incoming);
    }

    float3 Direction(float2 u) {
        return CosineHemisphere(u);
    }
}
