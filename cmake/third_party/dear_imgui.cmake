# SPDX-License-Identifier: BSD-3-Clause
set(CHRONO_BATCH_RENDER_IMGUI_TARGET
    ""
    CACHE STRING
    "A shared library target of Dear ImGui's docking branch to use instead of Chrono Batch Render's own"
)

if(CHRONO_BATCH_RENDER_IMGUI_TARGET)
    if(NOT TARGET "${CHRONO_BATCH_RENDER_IMGUI_TARGET}")
        message(
            FATAL_ERROR
            "CHRONO_BATCH_RENDER_IMGUI_TARGET names ${CHRONO_BATCH_RENDER_IMGUI_TARGET}, which is no target"
        )
    endif()
    get_target_property(imgui_type "${CHRONO_BATCH_RENDER_IMGUI_TARGET}" TYPE)
    if(NOT imgui_type STREQUAL "SHARED_LIBRARY")
        message(
            FATAL_ERROR
            "CHRONO_BATCH_RENDER_IMGUI_TARGET must be a shared library, for Chrono Batch Render and its callers to share one "
            "ImGui context; ${CHRONO_BATCH_RENDER_IMGUI_TARGET} is a ${imgui_type}"
        )
    endif()
    get_target_property(
        imgui_includes
        "${CHRONO_BATCH_RENDER_IMGUI_TARGET}"
        INTERFACE_INCLUDE_DIRECTORIES
    )
    string(
        REGEX REPLACE "\\$<BUILD_INTERFACE:([^>]*)>"
        "\\1"
        imgui_includes
        "${imgui_includes}"
    )
    find_path(
        CHRONO_BATCH_RENDER_IMGUI_SOURCE_DIR
        backends/imgui_impl_vulkan.cpp
        PATHS
            ${imgui_includes}
        NO_DEFAULT_PATH
    )
    if(NOT CHRONO_BATCH_RENDER_IMGUI_SOURCE_DIR)
        message(
            FATAL_ERROR
            "No ImGui sources with backends/ beside ${CHRONO_BATCH_RENDER_IMGUI_TARGET}'s headers; set "
            "CHRONO_BATCH_RENDER_IMGUI_SOURCE_DIR to them"
        )
    endif()
    set(imgui "${CHRONO_BATCH_RENDER_IMGUI_SOURCE_DIR}")
    add_library(chrono_batch_render_imgui INTERFACE)
    target_link_libraries(chrono_batch_render_imgui INTERFACE "${CHRONO_BATCH_RENDER_IMGUI_TARGET}")
    target_include_directories(chrono_batch_render_imgui SYSTEM INTERFACE "${imgui}/misc/cpp")
    set(chrono_batch_render_imgui_stdlib "${imgui}/misc/cpp/imgui_stdlib.cpp")
else()
    FetchContent_Declare(
        dear_imgui
        URL
            https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.tar.gz
        URL_HASH SHA256=90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc
    )
    FetchContent_MakeAvailable(dear_imgui)

    set(imgui "${dear_imgui_SOURCE_DIR}")
    add_library(
        chrono_batch_render_imgui
        OBJECT
        "${imgui}/imgui.cpp"
        "${imgui}/imgui_demo.cpp"
        "${imgui}/imgui_draw.cpp"
        "${imgui}/imgui_tables.cpp"
        "${imgui}/imgui_widgets.cpp"
        "${imgui}/misc/cpp/imgui_stdlib.cpp"
    )
    target_include_directories(
        chrono_batch_render_imgui
        SYSTEM
        PUBLIC
            "${imgui}"
            "${imgui}/misc/cpp"
    )
    target_include_directories(
        chrono_batch_render_imgui
        PUBLIC
            "${PROJECT_SOURCE_DIR}"
            "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}"
    )
    target_compile_definitions(
        chrono_batch_render_imgui
        PUBLIC
            IMGUI_DISABLE_OBSOLETE_FUNCTIONS
            "$<BUILD_INTERFACE:IMGUI_USER_CONFIG=\"chrono_batch_render/app/viewer/imgui_config.h\">"
        PRIVATE
            chrono_batch_render_viewer_EXPORTS
    )
    target_compile_options(chrono_batch_render_imgui PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
    set(chrono_batch_render_imgui_stdlib "")

    option(
        CHRONO_BATCH_RENDER_IMGUI_FREETYPE
        "Rasterize the viewer's fonts with FreeType when it's found"
        ON
    )
    if(CHRONO_BATCH_RENDER_IMGUI_FREETYPE)
        find_package(Freetype QUIET)
    endif()
    if(CHRONO_BATCH_RENDER_IMGUI_FREETYPE AND Freetype_FOUND)
        target_sources(
            chrono_batch_render_imgui
            PRIVATE
                "${imgui}/misc/freetype/imgui_freetype.cpp"
        )
        target_include_directories(chrono_batch_render_imgui SYSTEM PUBLIC "${imgui}/misc/freetype")
        target_compile_definitions(chrono_batch_render_imgui PUBLIC IMGUI_ENABLE_FREETYPE)
        target_link_libraries(chrono_batch_render_imgui PUBLIC Freetype::Freetype)
        message(STATUS "The viewer rasterizes its fonts with FreeType")
    endif()
endif()

add_library(
    chrono_batch_render_imgui_backends
    STATIC
    "${imgui}/backends/imgui_impl_glfw.cpp"
    "${imgui}/backends/imgui_impl_vulkan.cpp"
    ${chrono_batch_render_imgui_stdlib}
)
target_include_directories(chrono_batch_render_imgui_backends SYSTEM PUBLIC "${imgui}/backends")
target_compile_definitions(
    chrono_batch_render_imgui_backends
    PUBLIC
        IMGUI_IMPL_VULKAN_USE_VOLK
    PRIVATE
        GLFW_INCLUDE_NONE
        chrono_batch_render_viewer_EXPORTS
)
target_link_libraries(
    chrono_batch_render_imgui_backends
    PUBLIC
        chrono_batch_render_imgui
        volk::volk
        glfw
)
target_compile_options(
    chrono_batch_render_imgui_backends
    PRIVATE
        "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
)
