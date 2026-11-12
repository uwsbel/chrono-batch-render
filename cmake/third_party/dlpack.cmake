# SPDX-License-Identifier: BSD-3-Clause
set(CHRONO_BATCH_RENDER_DLPACK_TARGET
    ""
    CACHE STRING
    "A target of DLPack 1.0 or later, such as dlpack::dlpack, to use instead of Chrono Batch Render's own copy"
)

add_library(chrono_batch_render_dlpack INTERFACE)
if(CHRONO_BATCH_RENDER_DLPACK_TARGET)
    if(NOT TARGET "${CHRONO_BATCH_RENDER_DLPACK_TARGET}")
        message(
            FATAL_ERROR
            "CHRONO_BATCH_RENDER_DLPACK_TARGET names ${CHRONO_BATCH_RENDER_DLPACK_TARGET}, which is no target"
        )
    endif()
    target_link_libraries(
        chrono_batch_render_dlpack
        INTERFACE
            "${CHRONO_BATCH_RENDER_DLPACK_TARGET}"
    )
else()
    FetchContent_Declare(
        dlpack
        URL
            https://github.com/dmlc/dlpack/archive/refs/tags/v1.3.tar.gz
        URL_HASH SHA256=f3d567f885f6c142183afc91a58873b31d0e0b36faa2e45c232b98c74596404f
        SOURCE_SUBDIR none
    )
    FetchContent_MakeAvailable(dlpack)
    target_include_directories(
        chrono_batch_render_dlpack
        SYSTEM
        INTERFACE
            "${dlpack_SOURCE_DIR}/include"
    )
endif()
