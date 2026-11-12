// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.device.kernels.device;
__exported import chrono_batch_render.render.kernels.spectrum.spectrum;

interface IClosurePrimitive {
    static bool Glossy();

    [Differentiable]
    static Spectrum Estimate<Spectrum : ISpectrum>(Spectrum tint, float alpha, no_diff float4 params,
                                                   no_diff float3 outgoing);

    [Differentiable]
    static Spectrum Evaluate<Spectrum : ISpectrum>(Spectrum tint, float alpha, no_diff float4 params,
                                                   no_diff float3 outgoing,
                                                   no_diff float3 incoming);

    static float Pdf(float alpha, float4 params, float3 outgoing, float3 incoming);

    static float3 Direction(float alpha, float4 params, float3 outgoing, float2 u);
}

[__AttributeUsage(_AttributeTargets.Struct)]
struct ClosurePrimitiveAttribute {};
