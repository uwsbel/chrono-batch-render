# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_bvh
    builder.cpp
    common.cpp
    hardware_builder.cpp
    inspect.cpp
    lbvh.cpp
    reference.cpp
    software_builder.cpp
)
target_link_libraries(chrono_batch_render_bvh PUBLIC chrono_batch_render_algorithms)
chrono_batch_render_embed(
    chrono_batch_render_bvh
    LIBRARY kBvhKernels
    USES
        kAlgorithmsKernels
    HEADER chrono_batch_render/bvh/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/bvh/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/bvh/kernels/|*.h"
)

chrono_batch_render_static_library(
    chrono_batch_render_bvh_internal
    chrono_batch_render_bvh
    chrono_batch_render_algorithms_internal
)
