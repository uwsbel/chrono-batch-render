# SPDX-License-Identifier: BSD-3-Clause
include("${PROJECT_SOURCE_DIR}/cmake/third_party/openxr.cmake")

chrono_batch_render_object_library(
    chrono_batch_render_xr_objects
    EXPORTS chrono_batch_render_xr_EXPORTS
    interaction/draw.cpp
    interaction/grabbing.cpp
    interaction/locomotion.cpp
    interaction/pointing.cpp
    interaction/ui.cpp
    interaction/pointers.cpp
    rig/anchors.cpp
    rig/rig.cpp
    runtime/openxr.cpp
    runtime/openxr_input.cpp
    runtime/openxr_swapchains.cpp
    runtime/openxr_tracking.cpp
    runtime/profiles.cpp
    runtime/simulated.cpp
    recording.cpp
    run.cpp
    session.cpp
    stats.cpp
)
target_link_libraries(
    chrono_batch_render_xr_objects
    PRIVATE
        chrono_batch_render
        OpenXR::openxr_loader
        OpenXR::headers
        Vulkan::Headers
)

chrono_batch_render_shared_library(
    chrono_batch_render_xr
    chrono_batch_render_xr_objects
    OpenXR::openxr_loader
)
set_target_properties(
    chrono_batch_render_xr
    PROPERTIES
        DEFINE_SYMBOL
            chrono_batch_render_xr_EXPORTS
        EXPORT_NAME
            xr
)
target_link_libraries(chrono_batch_render_xr PUBLIC chrono_batch_render)
add_library(chrono_batch_render::xr ALIAS chrono_batch_render_xr)
include(GenerateExportHeader)
set(chrono_batch_render_export_msvc "#ifdef _MSC_VER\n#pragma warning(disable : 4251 4275)\n#endif")
generate_export_header(
    chrono_batch_render_xr
    BASE_NAME chrono_batch_render_xr
    EXPORT_MACRO_NAME CHRONO_BATCH_RENDER_XR_API
    EXPORT_FILE_NAME "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/xr_export.h"
    CUSTOM_CONTENT_FROM_VARIABLE chrono_batch_render_export_msvc
)

add_library(chrono_batch_render_xr_internal STATIC)
target_link_libraries(
    chrono_batch_render_xr_internal
    PUBLIC
        chrono_batch_render_xr_objects
        chrono_batch_render
        OpenXR::openxr_loader
)
target_link_options(
    chrono_batch_render_xr_internal
    INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:/NOIMPLIB;/NOEXP>"
        "$<$<BOOL:${CHRONO_BATCH_RENDER_COVERAGE}>:--coverage>"
)
target_compile_definitions(
    chrono_batch_render_xr_internal
    INTERFACE
        CHRONO_BATCH_RENDER_XR_STATIC_DEFINE
)

chrono_batch_render_public_files(
    frame.h
    recording.h
    rig.h
    run.h
    session.h
    ui.h
)

file(
    GLOB chrono_batch_render_xr_files
    RELATIVE "${PROJECT_SOURCE_DIR}"
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interaction/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rig/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/rig/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp"
)
chrono_batch_render_check_includes(chrono_batch_render/xr ${chrono_batch_render_xr_files})

if(CHRONO_BATCH_RENDER_BUILD_PYTHON)
    if(SKBUILD)
        set_property(TARGET chrono_batch_render_xr PROPERTY VERSION)
        set_property(TARGET chrono_batch_render_xr PROPERTY SOVERSION)
    endif()
    install(
        TARGETS
            chrono_batch_render_xr
        RUNTIME
            DESTINATION chrono_batch_render
            COMPONENT python
        LIBRARY
            DESTINATION chrono_batch_render
            COMPONENT python
            NAMELINK_SKIP
    )
endif()
