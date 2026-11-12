# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(chrono_batch_render_texture cache.cpp)
target_link_libraries(chrono_batch_render_texture PUBLIC chrono_batch_render_io)
chrono_batch_render_embed(
    chrono_batch_render_texture
    LIBRARY kTextureKernels
    USES
        kIoKernels
    HEADER chrono_batch_render/texture/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/texture/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/texture/kernels/|*.h"
)

chrono_batch_render_static_library(
    chrono_batch_render_texture_internal
    chrono_batch_render_texture
    chrono_batch_render_io_internal
)
