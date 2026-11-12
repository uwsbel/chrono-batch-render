# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_render
    ray_cast/ray_cast.cpp
    render_output.cpp
    renderer.cpp
    sensor_pipeline_runner.cpp
    sensor_pipelines.cpp
    shaders.cpp
)
target_link_libraries(
    chrono_batch_render_render
    PUBLIC
        chrono_batch_render_scene
        chrono_batch_render_overlay
        chrono_batch_render_sensor_pipeline
)
chrono_batch_render_embed(
    chrono_batch_render_render
    LIBRARY kRenderKernels
    USES
        kSceneKernels
    HEADER chrono_batch_render/render/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/render/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/render/kernels/|*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/imports|chrono_batch_render/|*.slang"
)

chrono_batch_render_static_library(
    chrono_batch_render_render_internal
    chrono_batch_render_render
    chrono_batch_render_overlay
    chrono_batch_render_sensor_pipeline_internal
    chrono_batch_render_scene_internal
    chrono_batch_render_fonts
    chrono_batch_render_stb
)

chrono_batch_render_public_files(
    ray_cast/ray_cast.h
    render_output.h
    renderer.h
    sensor_pipeline_runner.h
    shaders.h
)
