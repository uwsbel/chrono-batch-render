# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_app
    EXPORTS chrono_batch_render_app_EXPORTS
    cli.cpp
)
target_link_libraries(
    chrono_batch_render_app
    PUBLIC
        chrono_batch_render
        ${CMAKE_DL_LIBS}
)
target_compile_definitions(
    chrono_batch_render_app
    PRIVATE
        CHRONO_BATCH_RENDER_SOVERSION="${PROJECT_VERSION_MAJOR}"
)
if(CHRONO_BATCH_RENDER_BUILD_XR)
    target_compile_definitions(
        chrono_batch_render_app
        PUBLIC
            CHRONO_BATCH_RENDER_APP_XR
        PRIVATE
            CHRONO_BATCH_RENDER_XR_FILE="$<TARGET_FILE_NAME:chrono_batch_render_xr>"
    )
    add_dependencies(chrono_batch_render_app chrono_batch_render_xr)
endif()

add_executable(chrono_batch_render_cli main.cpp)
chrono_batch_render_warnings(chrono_batch_render_cli)
chrono_batch_render_coverage(chrono_batch_render_cli)
set_target_properties(
    chrono_batch_render_cli
    PROPERTIES
        OUTPUT_NAME
            chrono-batch-render
)
target_link_libraries(chrono_batch_render_cli PRIVATE chrono_batch_render_app)

set(chrono_batch_render_app_checked
    chrono_batch_render/app/cli.h
    chrono_batch_render/app/main.cpp
)
if(CHRONO_BATCH_RENDER_BUILD_XR)
    list(APPEND chrono_batch_render_app_checked chrono_batch_render/app/cli.cpp)
endif()
chrono_batch_render_check_includes(chrono_batch_render/app ${chrono_batch_render_app_checked})

if(CHRONO_BATCH_RENDER_BUILD_VIEWER)
    include("${PROJECT_SOURCE_DIR}/cmake/third_party/json.cmake")
    add_subdirectory(viewer)
    target_compile_definitions(
        chrono_batch_render_app
        PRIVATE
            CHRONO_BATCH_RENDER_VIEWER_FILE="$<TARGET_FILE_NAME:chrono_batch_render_viewer>"
    )
endif()
