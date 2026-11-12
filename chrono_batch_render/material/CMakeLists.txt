# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_material
    codegen.cpp
    node.cpp
    uplift.cpp
)
target_link_libraries(chrono_batch_render_material PUBLIC chrono_batch_render_graph)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/uplift.cpp" chrono_batch_render_uplift_source)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/uplift.h" chrono_batch_render_uplift_header)
string(
    SHA256 chrono_batch_render_uplift_digest
    "${chrono_batch_render_uplift_source}${chrono_batch_render_uplift_header}"
)
string(SUBSTRING "${chrono_batch_render_uplift_digest}" 0 16 chrono_batch_render_uplift_digest)
set_property(
    DIRECTORY
    APPEND
    PROPERTY
        CMAKE_CONFIGURE_DEPENDS
            uplift.cpp
            uplift.h
)
set_property(
    SOURCE
        uplift.cpp
    APPEND
    PROPERTY
        COMPILE_DEFINITIONS
            CHRONO_BATCH_RENDER_UPLIFT_DIGEST="${chrono_batch_render_uplift_digest}"
)
foreach(
    template
    dispatch
    definition
    surface
    standard
    registered
)
    chrono_batch_render_embed(
        chrono_batch_render_material
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/templates/${template}.slang.in"
        SYMBOL chrono_batch_render_material_${template}
    )
endforeach()

chrono_batch_render_static_library(
    chrono_batch_render_material_internal
    chrono_batch_render_material
    chrono_batch_render_graph_internal
)
