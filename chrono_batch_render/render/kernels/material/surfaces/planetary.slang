// SPDX-License-Identifier: BSD-3-Clause

import chrono_batch_render.material;

struct HapkeSurface {
    [Color] SpectralValue albedo = { 1, 1, float3(0.3, 0.3, 0.3) };
    [Input(0, 0.99)] float b = 0.25;
    [Input(-1, 1)] float c = 0.5;
    [Input(0, 6)] float bs0 = 1;
    [Input(0.001, 0.5)] float hs = 0.07;
    [Input(0, 45)] float thetaBar = 20;
    [Input(0, 0.74)] float phi = 0.3;

    void Shade<Spectrum : ISpectrum>(ShadeContext context, inout OpticalClosure<Spectrum> s) {
        const Lobe<Spectrum> lobe = Lobe<Spectrum>.Hapke(albedo.Evaluate<Spectrum>(),
                                                         radians(thetaBar), b, c, phi, bs0, hs, 1);
        s.AddLobe(lobe);
        s.albedo = lobe.Estimate(float3(0, 0, 1));
    }
}

struct LommelSeeligerSurface {
    [Color] SpectralValue albedo = { 1, 1, float3(0.3, 0.3, 0.3) };
    [Input(-0.95, 0.95)] float xi = 0;

    void Shade<Spectrum : ISpectrum>(ShadeContext context, inout OpticalClosure<Spectrum> s) {
        const Lobe<Spectrum> lobe =
            Lobe<Spectrum>.LommelSeeliger(albedo.Evaluate<Spectrum>(), xi, 1);
        s.AddLobe(lobe);
        s.albedo = lobe.Estimate(float3(0, 0, 1));
    }
}

struct LunarLambertSurface {
    [Color] SpectralValue albedo = { 1, 1, float3(0.12, 0.12, 0.12) };
    [Input(-0.05, 0.05)] float a1 = -0.019;
    [Input(-0.001, 0.001)] float a2 = 0.000242;
    [Input(-0.00001, 0.00001)] float a3 = -0.00000146;

    void Shade<Spectrum : ISpectrum>(ShadeContext context, inout OpticalClosure<Spectrum> s) {
        const Lobe<Spectrum> lobe =
            Lobe<Spectrum>.LunarLambert(albedo.Evaluate<Spectrum>(), float3(a1, a2, a3), 1);
        s.AddLobe(lobe);
        s.albedo = lobe.Estimate(float3(0, 0, 1));
    }
}

struct OrenNayarSurface {
    [Color] SpectralValue albedo = { 1, 1, float3(0.5, 0.5, 0.5) };
    [Input(0, 60)] float sigma = 20;

    void Shade<Spectrum : ISpectrum>(ShadeContext context, inout OpticalClosure<Spectrum> s) {
        const Lobe<Spectrum> lobe =
            Lobe<Spectrum>.OrenNayar(albedo.Evaluate<Spectrum>(), radians(sigma), 1);
        s.AddLobe(lobe);
        s.albedo = lobe.Estimate(float3(0, 0, 1));
    }
}
