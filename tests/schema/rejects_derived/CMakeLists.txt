# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.25)
project(ChronoBatchRenderSchemaRejectsDerived NONE)
include("${CHRONO_BATCH_RENDER_ROOT}/cmake/ChronoBatchRenderSchema.cmake")
file(
    WRITE "${CMAKE_BINARY_DIR}/bad.schema"
    "class BadThing : Typed (\n    doc = \"A prim with a derived attribute.\"\n) {\n"
    "    float chrono:computed = 0 (\n        derived\n        doc = \"Computed.\"\n    )\n}\n"
)
chrono_batch_render_generate_schemas(
    "${CMAKE_BINARY_DIR}/generated"
    "${CHRONO_BATCH_RENDER_ROOT}/chrono_batch_render/schema/geom.schema"
    "${CHRONO_BATCH_RENDER_ROOT}/chrono_batch_render/schema/lux.schema"
    "${CHRONO_BATCH_RENDER_ROOT}/chrono_batch_render/schema/sensor.schema"
    "${CHRONO_BATCH_RENDER_ROOT}/chrono_batch_render/schema/render.schema"
    "${CHRONO_BATCH_RENDER_ROOT}/chrono_batch_render/schema/shade.schema"
    "${CMAKE_BINARY_DIR}/bad.schema"
)
