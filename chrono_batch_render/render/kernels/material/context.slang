// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.render.kernels.query.material;
__exported import chrono_batch_render.render.kernels.surface.point;
import chrono_batch_render.render.kernels.spectrum.band;

using namespace chrono::batch_render;

struct ShadeContext {
    MaterialQuery materials;
    uint material;
    uint definition;
    uint words;
    SurfacePoint point;
    float3 outgoing;

    __init(MaterialQuery materials, uint material, SurfacePoint point, float3 outgoing) {
        this.materials = materials;
        this.material = material;
        UseSpectra(materials.hit.tables);
        const MaterialRecord record = materials.Record(material);
        definition = record.definition;
        words = record.words;
        this.point = point;
        this.outgoing = outgoing;
    }
}
