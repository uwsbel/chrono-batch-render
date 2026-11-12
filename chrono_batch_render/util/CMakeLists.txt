# SPDX-License-Identifier: BSD-3-Clause
set(chrono_batch_render_build_info "${CMAKE_CURRENT_BINARY_DIR}/build_info.h")
add_custom_target(
    chrono_batch_render_build_info
    ALL
    COMMAND
        "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT=${chrono_batch_render_build_info}" "-DVERSION=${PROJECT_VERSION}"
        "-DPRERELEASE=${CHRONO_BATCH_RENDER_PRERELEASE}" -P
        "${PROJECT_SOURCE_DIR}/cmake/ChronoBatchRenderBuildInfo.cmake"
    BYPRODUCTS
        "${chrono_batch_render_build_info}"
    VERBATIM
)

chrono_batch_render_object_library(
    chrono_batch_render_util
    disk_cache.cpp
    environment.cpp
    error.cpp
    library.cpp
    log.cpp
    memory.cpp
    profiler.cpp
    text.cpp
    thread_pool.cpp
    version.cpp
)
add_dependencies(chrono_batch_render_util chrono_batch_render_build_info)
target_include_directories(chrono_batch_render_util PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(
    chrono_batch_render_util
    PRIVATE
        chrono_batch_render_nvtx
        ${CMAKE_DL_LIBS}
)
chrono_batch_render_static_library(
    chrono_batch_render_util_internal
    chrono_batch_render_util
    ${CMAKE_DL_LIBS}
)

chrono_batch_render_public_files(
    error.h
    log.h
    memory.h
    profiler.h
    span.h
    version.h
)
