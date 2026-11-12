# SPDX-License-Identifier: BSD-3-Clause
include("${PROJECT_SOURCE_DIR}/cmake/third_party/glfw.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/third_party/dear_imgui.cmake")

set(chrono_batch_render_viewport_source "${CMAKE_CURRENT_SOURCE_DIR}/viewport/viewport.slang")
set(chrono_batch_render_viewport_spirv "${CMAKE_CURRENT_BINARY_DIR}/viewport.spv")
add_custom_command(
    OUTPUT
        "${chrono_batch_render_viewport_spirv}"
    COMMAND
        "${slang_SOURCE_DIR}/bin/slangc" "${chrono_batch_render_viewport_source}" -target spirv
        -profile spirv_1_5 -fvk-use-entrypoint-name -entry VertexMain -stage vertex -entry
        FragmentMain -stage fragment -o "${chrono_batch_render_viewport_spirv}"
    DEPENDS
        "${chrono_batch_render_viewport_source}"
    VERBATIM
)

chrono_batch_render_object_library(
    chrono_batch_render_viewer_objects
    EXPORTS chrono_batch_render_viewer_EXPORTS
    overlays/hud.cpp
    overlays/toolbar.cpp
    panels/capture.cpp
    panels/help.cpp
    panels/memory.cpp
    panels/profiler.cpp
    panels/properties.cpp
    panels/stage.cpp
    panels/status.cpp
    panels/xr_loader.cpp
    presenter/vulkan_presenter.cpp
    shell/capture.cpp
    shell/editor.cpp
    theme/theme.cpp
    viewer.cpp
    viewport/orbit_camera.cpp
    viewport/gizmo.cpp
    viewport/viewport.cpp
    widgets/asset_path.cpp
    widgets/file_dialog.cpp
    widgets/import_dialog.cpp
    widgets/widgets.cpp
)
chrono_batch_render_embed(
    chrono_batch_render_viewer_objects
    FILE "${chrono_batch_render_viewport_spirv}"
    SYMBOL chrono_batch_render_viewport_spirv
)
if(CHRONO_BATCH_RENDER_BUILD_XR)
    target_compile_definitions(
        chrono_batch_render_viewer_objects
        PRIVATE
            CHRONO_BATCH_RENDER_VIEWER_FILE="$<TARGET_FILE_NAME:chrono_batch_render_viewer>"
            CHRONO_BATCH_RENDER_VIEWER_XR_FILE="$<TARGET_FILE_NAME:chrono_batch_render_viewer_xr>"
    )
endif()
target_compile_definitions(
    chrono_batch_render_viewer_objects
    PRIVATE
        VK_NO_PROTOTYPES
        GLFW_INCLUDE_NONE
)
target_link_libraries(
    chrono_batch_render_viewer_objects
    PUBLIC
        chrono_batch_render_imgui
    PRIVATE
        chrono_batch_render
        chrono_batch_render_imgui_backends
        chrono_batch_render_fonts
        chrono_batch_render_json
        Vulkan::Headers
        ${CMAKE_DL_LIBS}
)

chrono_batch_render_shared_library(
    chrono_batch_render_viewer
    chrono_batch_render_viewer_objects
    ${CMAKE_DL_LIBS}
    chrono_batch_render_imgui
    chrono_batch_render_imgui_backends
    chrono_batch_render_fonts
)
set_target_properties(
    chrono_batch_render_viewer
    PROPERTIES
        DEFINE_SYMBOL
            chrono_batch_render_viewer_EXPORTS
        EXPORT_NAME
            viewer
)
target_link_libraries(
    chrono_batch_render_viewer
    PUBLIC
        chrono_batch_render
    INTERFACE
        "$<BUILD_INTERFACE:chrono_batch_render_imgui>"
)
if(NOT CHRONO_BATCH_RENDER_IMGUI_TARGET)
    target_include_directories(
        chrono_batch_render_viewer
        INTERFACE
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render/third_party/imgui>"
    )
endif()
add_library(chrono_batch_render::viewer ALIAS chrono_batch_render_viewer)
include(GenerateExportHeader)
set(chrono_batch_render_export_msvc "#ifdef _MSC_VER\n#pragma warning(disable : 4251 4275)\n#endif")
generate_export_header(
    chrono_batch_render_viewer
    BASE_NAME chrono_batch_render_viewer
    EXPORT_MACRO_NAME CHRONO_BATCH_RENDER_VIEWER_API
    EXPORT_FILE_NAME
        "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/viewer_export.h"
    CUSTOM_CONTENT_FROM_VARIABLE chrono_batch_render_export_msvc
)

add_library(chrono_batch_render_viewer_internal STATIC)
target_link_libraries(
    chrono_batch_render_viewer_internal
    PUBLIC
        chrono_batch_render_viewer_objects
        chrono_batch_render_imgui
        chrono_batch_render_imgui_backends
        chrono_batch_render_fonts
        chrono_batch_render
        ${CMAKE_DL_LIBS}
)
target_link_options(
    chrono_batch_render_viewer_internal
    INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:/NOIMPLIB;/NOEXP>"
        "$<$<BOOL:${CHRONO_BATCH_RENDER_COVERAGE}>:--coverage>"
)
target_compile_definitions(
    chrono_batch_render_viewer_internal
    INTERFACE
        CHRONO_BATCH_RENDER_VIEWER_STATIC_DEFINE
        VK_NO_PROTOTYPES
        GLFW_INCLUDE_NONE
)
target_link_libraries(chrono_batch_render_viewer_internal PUBLIC Vulkan::Headers)

if(CHRONO_BATCH_RENDER_BUILD_XR)
    chrono_batch_render_object_library(
        chrono_batch_render_viewer_xr_objects
        EXPORTS chrono_batch_render_viewer_xr_EXPORTS
        panels/xr.cpp
    )
    target_compile_definitions(
        chrono_batch_render_viewer_xr_objects
        PRIVATE
            VK_NO_PROTOTYPES
            GLFW_INCLUDE_NONE
    )
    target_link_libraries(
        chrono_batch_render_viewer_xr_objects
        PRIVATE
            chrono_batch_render_viewer
            chrono_batch_render_xr
            chrono_batch_render
            Vulkan::Headers
    )
    chrono_batch_render_shared_library(
        chrono_batch_render_viewer_xr
        chrono_batch_render_viewer_xr_objects
        chrono_batch_render_viewer
        chrono_batch_render_xr
    )
    set_target_properties(
        chrono_batch_render_viewer_xr
        PROPERTIES
            DEFINE_SYMBOL
                chrono_batch_render_viewer_xr_EXPORTS
    )
    target_link_libraries(chrono_batch_render_viewer_xr PUBLIC chrono_batch_render_viewer)
    generate_export_header(
        chrono_batch_render_viewer_xr
        BASE_NAME chrono_batch_render_viewer_xr
        EXPORT_MACRO_NAME CHRONO_BATCH_RENDER_VIEWER_XR_API
        EXPORT_FILE_NAME
            "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/viewer_xr_export.h"
        CUSTOM_CONTENT_FROM_VARIABLE chrono_batch_render_export_msvc
    )
endif()

file(
    GLOB_RECURSE chrono_batch_render_viewer_files
    RELATIVE "${PROJECT_SOURCE_DIR}"
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
if(NOT CHRONO_BATCH_RENDER_BUILD_XR)
    list(
        REMOVE_ITEM chrono_batch_render_viewer_files
        chrono_batch_render/app/viewer/panels/xr.cpp
        chrono_batch_render/app/viewer/panels/xr.h
    )
endif()
chrono_batch_render_check_includes(chrono_batch_render/app ${chrono_batch_render_viewer_files})

if(CHRONO_BATCH_RENDER_BUILD_PYTHON)
    if(SKBUILD)
        set_property(TARGET chrono_batch_render_viewer PROPERTY VERSION)
        set_property(TARGET chrono_batch_render_viewer PROPERTY SOVERSION)
    endif()
    install(
        TARGETS
            chrono_batch_render_viewer
        RUNTIME
            DESTINATION chrono_batch_render
            COMPONENT python
        LIBRARY
            DESTINATION chrono_batch_render
            COMPONENT python
            NAMELINK_SKIP
    )
    if(CHRONO_BATCH_RENDER_BUILD_XR)
        if(SKBUILD)
            set_property(TARGET chrono_batch_render_viewer_xr PROPERTY VERSION)
            set_property(TARGET chrono_batch_render_viewer_xr PROPERTY SOVERSION)
        endif()
        install(
            TARGETS
                chrono_batch_render_viewer_xr
            RUNTIME
                DESTINATION chrono_batch_render
                COMPONENT python
            LIBRARY
                DESTINATION chrono_batch_render
                COMPONENT python
                NAMELINK_SKIP
        )
    endif()
endif()
