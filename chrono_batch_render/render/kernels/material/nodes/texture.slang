// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.material.mapping;
import chrono_batch_render.render.kernels.material.context;
import chrono_batch_render.render.kernels.material.spectral;

using namespace chrono::batch_render;

SurfaceTexels<Inputs> SampleNodeTextures<let Inputs : uint>(MaterialQuery query,
                                                           SurfacePoint point,
                                                           Ptr<MappingRecord> mapping,
                                                           Ptr<TextureInput> inputs) {
    SurfaceTexels<Inputs> texels = SurfaceTexels<Inputs>(query, point.shadingNormal);
    texels.inputs = inputs;
    PlacementSettings settings;
    settings.uv.angle = radians(mapping.uvRotation);
    settings.uv.scale = mapping.uvScale.Vector();
    settings.uv.offset = mapping.uvOffset.Vector();
    settings.fine = false;
    settings.single = query.minimal;
    settings.hex = mapping.hexTiling != 0;
    settings.hexContrast = mapping.hexTilingContrast;
    settings.hexRotation = mapping.hexTilingRotation / 180;
    settings.frameNormal = point.shadingNormal;
    ForEachPlacement(query.hit, point, *mapping, settings, texels);
    return texels;
}

SurfaceTexels<Inputs> SampleNodeTextures<let Inputs : uint>(ShadeContext context,
                                                           Ptr<MappingRecord> mapping,
                                                           Ptr<TextureInput> inputs) {
    return SampleNodeTextures<Inputs>(context.materials, context.point, mapping, inputs);
}

[noinline]
float3 MapNodeDetail(MaterialQuery query, Ptr<MappingRecord> mapping, Ptr<DetailRecord> detail,
                     Ptr<TextureInput> inputs, inout SurfacePoint point) {
    const bool detailed = detail.enableDetail != 0;
    const float2 tilt = detail.normal.Vector().xy;
    const bool bent = tilt.x != 0 || tilt.y != 0;
    const bool mapped = inputs[0].handle != 0;
    if (!detailed && !bent && !mapped) {
        return float3(1);
    }
    SurfaceTexels<1> base = SurfaceTexels<1>(query, point.shadingNormal);
    base.SetNormal(&inputs[0], detail.normal.Vector(), detail.normalStrength);
    PlacementSettings settings;
    settings.uv.angle = radians(mapping.uvRotation);
    settings.uv.scale = mapping.uvScale.Vector();
    settings.uv.offset = mapping.uvOffset.Vector();
    settings.fine = false;
    settings.single = query.minimal;
    settings.hex = mapping.hexTiling != 0;
    settings.hexContrast = mapping.hexTilingContrast;
    settings.hexRotation = mapping.hexTilingRotation / 180;
    settings.frameNormal = point.shadingNormal;
    ForEachPlacement(query.hit, point, *mapping, settings, base);
    float3 normal = base.MappedNormal();
    float3 overlay = float3(1);
    if (detailed) {
        SurfaceTexels<1> fine = SurfaceTexels<1>(query, normal);
        fine.inputs = &inputs[2];
        fine.SetNormal(&inputs[1], detail.detailNormal.Vector(), detail.detailNormalStrength);
        settings.fine = true;
        settings.detail.angle = radians(detail.detailRotation);
        settings.detail.scale = detail.detailScale.Vector();
        settings.detail.offset = detail.detailOffset.Vector();
        settings.hex = detail.detailHexTiling != 0;
        settings.frameNormal = normal;
        ForEachPlacement(query.hit, point, *mapping, settings, fine);
        normal = fine.MappedNormal();
        const float3 texel =
            TexturedColor(detail.detailAlbedo, fine.texels[0], fine.Handle(0), &inputs[2]);
        overlay = lerp(float3(1), 2 * texel, detail.detailAlbedoStrength);
    }
    if (dot(normal, point.shadingNormal) > 0) {
        point.shadingNormal = normal;
    }
    return overlay;
}

SpectralValue DetailedAlbedo(SpectralValue value, float3 overlay) {
    if (value.kind == kSpectralRgb) {
        SpectralValue tinted = value;
        tinted.data = value.data * overlay;
        return tinted;
    }
    return value.Scaled(dot(overlay, kLuminanceWeights));
}
