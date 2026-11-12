# SPDX-License-Identifier: BSD-3-Clause
include(GenerateExportHeader)
include("${PROJECT_SOURCE_DIR}/cmake/ChronoBatchRenderLibraries.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/ChronoBatchRenderSchema.cmake")

set(chrono_batch_render_python_schemas "")
if(CHRONO_BATCH_RENDER_BUILD_PYTHON)
    set(chrono_batch_render_python_defines
        "${PROJECT_BINARY_DIR}/chrono_batch_render/python/defines.cpp"
    )
    set(chrono_batch_render_python_schemas
        PYTHON
        "${PROJECT_BINARY_DIR}/python/chrono_batch_render/schemas.py"
        PYTHON_DEFINES
        "${chrono_batch_render_python_defines}"
    )
endif()
set(chrono_batch_render_scene_defines "${PROJECT_BINARY_DIR}/chrono_batch_render/scene/defines.cpp")
chrono_batch_render_generate_schemas(
    "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/geom.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/lux.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/sensor.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/render.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/shade.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/sensor_pipeline.schema"
    DEFINE_HEADER "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/defines.h"
    DEFINE_SOURCE "${chrono_batch_render_scene_defines}"
    TYPED_HEADER "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/typed.h"
    ${chrono_batch_render_python_schemas}
)

add_subdirectory(util)
add_subdirectory(device)
add_subdirectory(algorithms)
add_subdirectory(bvh)
add_subdirectory(io)
add_subdirectory(texture)
add_subdirectory(graph)
add_subdirectory(sensor_pipeline)
add_subdirectory(material)
add_subdirectory(stage)
add_subdirectory(scene)
add_subdirectory(overlay)
add_subdirectory(render)
add_subdirectory(import)

chrono_batch_render_public_files(chrono_batch_render.h)

chrono_batch_render_shared_library(
    chrono_batch_render
    chrono_batch_render_util
    chrono_batch_render_device
    chrono_batch_render_algorithms
    chrono_batch_render_bvh
    chrono_batch_render_texture
    chrono_batch_render_graph
    chrono_batch_render_sensor_pipeline
    chrono_batch_render_material
    chrono_batch_render_stage
    chrono_batch_render_scene
    chrono_batch_render_overlay
    chrono_batch_render_render
    chrono_batch_render_io
    chrono_batch_render_import
    slang::slang
)
add_library(chrono_batch_render::chrono_batch_render ALIAS chrono_batch_render)
set(chrono_batch_render_export_msvc "#ifdef _MSC_VER\n#pragma warning(disable : 4251 4275)\n#endif")
generate_export_header(
    chrono_batch_render
    EXPORT_MACRO_NAME CHRONO_BATCH_RENDER_API
    EXPORT_FILE_NAME "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/export.h"
    CUSTOM_CONTENT_FROM_VARIABLE chrono_batch_render_export_msvc
)

chrono_batch_render_check_public_files()

if(CHRONO_BATCH_RENDER_BUILD_PYTHON)
    add_subdirectory(python)
endif()
