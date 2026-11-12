# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_sensor_pipeline
    codegen.cpp
    definition.cpp
    node.cpp
    resolve.cpp
)
target_link_libraries(chrono_batch_render_sensor_pipeline PUBLIC chrono_batch_render_graph)
foreach(
    template
    module
    elements
    captures
    reduce_groups
    reduce_partial
)
    chrono_batch_render_embed(
        chrono_batch_render_sensor_pipeline
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/templates/${template}.slang.in"
        SYMBOL chrono_batch_render_sensor_pipeline_${template}
    )
endforeach()

chrono_batch_render_static_library(
    chrono_batch_render_sensor_pipeline_internal
    chrono_batch_render_sensor_pipeline
    chrono_batch_render_graph_internal
)
