# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_algorithms
    radix_sort.cpp
    scan.cpp
)
target_link_libraries(chrono_batch_render_algorithms PUBLIC chrono_batch_render_device)
chrono_batch_render_embed(
    chrono_batch_render_algorithms
    LIBRARY kAlgorithmsKernels
    USES
        kDeviceKernels
    HEADER chrono_batch_render/algorithms/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/algorithms/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/algorithms/kernels/|*.h"
)

chrono_batch_render_static_library(
    chrono_batch_render_algorithms_internal
    chrono_batch_render_algorithms
    chrono_batch_render_device_internal
)
