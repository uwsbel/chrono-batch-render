# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_import
    gltf.cpp
    importer.cpp
    obj.cpp
    usd.cpp
)
target_link_libraries(
    chrono_batch_render_import
    PUBLIC
        chrono_batch_render_scene
    PRIVATE
        chrono_batch_render_cgltf
        chrono_batch_render_tinyobjloader
        tinyusd::tinyusd
)

chrono_batch_render_static_library(
    chrono_batch_render_import_internal
    chrono_batch_render_import
    chrono_batch_render_scene_internal
    chrono_batch_render_cgltf
    chrono_batch_render_tinyobjloader
    tinyusd::tinyusd
)

chrono_batch_render_public_files(importer.h)
