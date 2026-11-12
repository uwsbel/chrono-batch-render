// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.material.idefinition;
import chrono_batch_render.render.kernels.material.nodes.texture;
import chrono_batch_render.render.kernels.material.spectral;
import chrono_batch_render.render.kernels.material.surfaces.planetary;

using namespace chrono::batch_render;

struct PlanetaryInputs {
    SpectralValue albedo;
    float values[kPlanetaryValues];
}

PlanetaryInputs ReadPlanetary(inout ShadeContext context) {
    SurfacePoint point = context.point;
    const PlanetaryInputs inputs =
        ReadPlanetaryInputs(context.materials, context.materials.Params<PlanetaryParams>(context.words),
                        point);
    context.point = point;
    return inputs;
}

[noinline]
PlanetaryInputs ReadPlanetaryInputs(MaterialQuery query, PlanetaryParams* p, inout SurfacePoint point) {
    const float3 overlay = MapNodeDetail(query, &p.mapping, &p.detail, &p.details[0], point);
    PlanetaryInputs inputs;
    float3 albedo = p.albedo.Vector();
    [ForceUnroll]
    for (uint i = 0; i < kPlanetaryValues; ++i) {
        inputs.values[i] = p.values[i];
    }
    if (p.textured != 0) {
        const SurfaceTexels<kPlanetaryInputs> texels =
            SampleNodeTextures<kPlanetaryInputs>(query, point, &p.mapping, &p.inputs[0]);
        albedo = TexturedColor(p.albedo, texels.texels[0], texels.Handle(0), &p.inputs[0]);
        [ForceUnroll]
        for (uint i = 0; i < kPlanetaryValues; ++i) {
            inputs.values[i] = TexturedFloat(p.values[i], texels.texels[i + 1],
                                             texels.Handle(i + 1), &p.inputs[i + 1]);
        }
    }
    const uint spectrum = p.inputs[0].spectrum;
    inputs.albedo = DetailedAlbedo(
        spectrum != kNoSpectrum ? SpectralValue.Library(spectrum) : SpectralValue.Rgb(albedo),
        overlay);
    return inputs;
}

struct HapkeDefinition<Spectrum : ISpectrum> : IMaterialDefinition<Spectrum> {
    static void Shade(inout ShadeContext context, inout OpticalClosure<Spectrum> closure) {
        const PlanetaryInputs inputs = ReadPlanetary(context);
        HapkeSurface node;
        node.albedo = inputs.albedo;
        node.b = inputs.values[0];
        node.c = inputs.values[1];
        node.bs0 = inputs.values[2];
        node.hs = inputs.values[3];
        node.thetaBar = inputs.values[4];
        node.phi = inputs.values[5];
        node.Shade<Spectrum>(context, closure);
    }

    static Spectrum Albedo(inout ShadeContext context) {
        return ClosureAlbedo<Spectrum, HapkeDefinition<Spectrum>>(context);
    }
}

struct LommelSeeligerDefinition<Spectrum : ISpectrum> : IMaterialDefinition<Spectrum> {
    static void Shade(inout ShadeContext context, inout OpticalClosure<Spectrum> closure) {
        const PlanetaryInputs inputs = ReadPlanetary(context);
        LommelSeeligerSurface node;
        node.albedo = inputs.albedo;
        node.xi = inputs.values[0];
        node.Shade<Spectrum>(context, closure);
    }

    static Spectrum Albedo(inout ShadeContext context) {
        return ClosureAlbedo<Spectrum, LommelSeeligerDefinition<Spectrum>>(context);
    }
}

struct LunarLambertDefinition<Spectrum : ISpectrum> : IMaterialDefinition<Spectrum> {
    static void Shade(inout ShadeContext context, inout OpticalClosure<Spectrum> closure) {
        const PlanetaryInputs inputs = ReadPlanetary(context);
        LunarLambertSurface node;
        node.albedo = inputs.albedo;
        node.a1 = inputs.values[0];
        node.a2 = inputs.values[1];
        node.a3 = inputs.values[2];
        node.Shade<Spectrum>(context, closure);
    }

    static Spectrum Albedo(inout ShadeContext context) {
        return ClosureAlbedo<Spectrum, LunarLambertDefinition<Spectrum>>(context);
    }
}

struct OrenNayarDefinition<Spectrum : ISpectrum> : IMaterialDefinition<Spectrum> {
    static void Shade(inout ShadeContext context, inout OpticalClosure<Spectrum> closure) {
        const PlanetaryInputs inputs = ReadPlanetary(context);
        OrenNayarSurface node;
        node.albedo = inputs.albedo;
        node.sigma = inputs.values[0];
        node.Shade<Spectrum>(context, closure);
    }

    static Spectrum Albedo(inout ShadeContext context) {
        return ClosureAlbedo<Spectrum, OrenNayarDefinition<Spectrum>>(context);
    }
}
