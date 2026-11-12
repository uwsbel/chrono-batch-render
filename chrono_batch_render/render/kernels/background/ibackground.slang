// SPDX-License-Identifier: BSD-3-Clause

__exported import chrono_batch_render.scene.records.render_tables;

using namespace chrono::batch_render;

interface IBackground {
    static This Load(ProductRecord* product);

    float3 Radiance(float3 direction);
}
