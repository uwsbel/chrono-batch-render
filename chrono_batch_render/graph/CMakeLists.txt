# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(chrono_batch_render_graph graph.cpp)
target_link_libraries(chrono_batch_render_graph PUBLIC chrono_batch_render_util)

chrono_batch_render_static_library(
    chrono_batch_render_graph_internal
    chrono_batch_render_graph
    chrono_batch_render_util_internal
)
