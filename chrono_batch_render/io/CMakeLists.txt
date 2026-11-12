# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_io
    ies.cpp
    image.cpp
)
target_link_libraries(
    chrono_batch_render_io
    PUBLIC
        chrono_batch_render_device
    PRIVATE
        chrono_batch_render_stb
        chrono_batch_render_openexr
)
chrono_batch_render_embed(
    chrono_batch_render_io
    LIBRARY kIoKernels
    USES
        kDeviceKernels
    HEADER chrono_batch_render/io/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/io/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/io/kernels/|*.h"
)

chrono_batch_render_static_library(
    chrono_batch_render_io_internal
    chrono_batch_render_io
    chrono_batch_render_device_internal
    chrono_batch_render_stb
    chrono_batch_render_openexr
)

chrono_batch_render_public_files(image.h)
