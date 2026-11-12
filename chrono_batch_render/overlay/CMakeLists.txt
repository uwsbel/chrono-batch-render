# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_overlay
    font.cpp
    overlay.cpp
)
target_link_libraries(
    chrono_batch_render_overlay
    PUBLIC
        chrono_batch_render_scene
    PRIVATE
        chrono_batch_render_fonts
        chrono_batch_render_stb
)
chrono_batch_render_embed(
    chrono_batch_render_overlay
    LIBRARY kOverlayKernels
    USES
        kRenderKernels
    HEADER chrono_batch_render/overlay/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/overlay/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/overlay/kernels/|*.h"
)

chrono_batch_render_public_files(
    kernels/style.h
    overlay.h
)
