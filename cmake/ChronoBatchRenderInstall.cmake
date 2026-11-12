# SPDX-License-Identifier: BSD-3-Clause
include(CMakePackageConfigHelpers)

set(chrono_batch_render_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/chrono_batch_render")
set(chrono_batch_render_third_party_dir
    "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render/third_party"
)
set(chrono_batch_render_private_libdir "${CMAKE_INSTALL_LIBDIR}/chrono_batch_render")
set(chrono_batch_render_viewer OFF)
if(TARGET chrono_batch_render_viewer)
    set(chrono_batch_render_viewer ON)
endif()

install(
    TARGETS
        chrono_batch_render_cli
    RUNTIME
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT runtime
)
install(
    TARGETS
        chrono_batch_render
    EXPORT chrono_batch_renderTargets
    RUNTIME
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT runtime
    LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        COMPONENT runtime
        NAMELINK_COMPONENT dev
    ARCHIVE
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        COMPONENT dev
)
if(chrono_batch_render_viewer)
    install(
        TARGETS
            chrono_batch_render_viewer
        EXPORT chrono_batch_renderViewerTargets
        RUNTIME
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT viewer
        LIBRARY
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT viewer
            NAMELINK_COMPONENT viewer_dev
        ARCHIVE
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT viewer_dev
    )
endif()
if(TARGET chrono_batch_render_xr)
    install(
        TARGETS
            chrono_batch_render_xr
        EXPORT chrono_batch_renderXrTargets
        RUNTIME
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT xr
        LIBRARY
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT xr
            NAMELINK_COMPONENT xr_dev
        ARCHIVE
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT xr_dev
    )
    install(
        DIRECTORY
            "${PROJECT_SOURCE_DIR}/chrono_batch_render/xr"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render"
        COMPONENT xr_dev
    )
    install(
        FILES
            "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/xr_export.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render"
        COMPONENT xr_dev
    )
    install(
        EXPORT chrono_batch_renderXrTargets
        NAMESPACE chrono_batch_render::
        DESTINATION "${chrono_batch_render_cmake_dir}"
        COMPONENT xr_dev
    )
endif()
if(TARGET chrono_batch_render_viewer_xr)
    install(
        TARGETS
            chrono_batch_render_viewer_xr
        RUNTIME
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT xr
        LIBRARY
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT xr
            NAMELINK_SKIP
    )
endif()
if(WIN32)
    install(
        FILES
            ${CHRONO_BATCH_RENDER_SLANG_RUNTIME}
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT runtime
    )
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT runtime)
    include(InstallRequiredSystemLibraries)
    foreach(
        target
        IN
        ITEMS
            chrono_batch_render
            chrono_batch_render_viewer
            chrono_batch_render_cli
            chrono_batch_render_xr
            chrono_batch_render_viewer_xr
    )
        if(TARGET ${target})
            set_target_properties(
                ${target}
                PROPERTIES
                    DEBUG_POSTFIX
                        d
            )
        endif()
    endforeach()
else()
    if(APPLE)
        set(chrono_batch_render_origin "@loader_path")
    else()
        set(chrono_batch_render_origin "$ORIGIN")
    endif()
    install(
        PROGRAMS
            ${CHRONO_BATCH_RENDER_SLANG_RUNTIME}
        DESTINATION "${chrono_batch_render_private_libdir}"
        COMPONENT runtime
    )
    set_target_properties(
        chrono_batch_render
        PROPERTIES
            INSTALL_RPATH
                "${chrono_batch_render_origin}/chrono_batch_render;${chrono_batch_render_origin}"
    )
    if(TARGET chrono_batch_render_xr)
        set_target_properties(
            chrono_batch_render_xr
            PROPERTIES
                INSTALL_RPATH
                    "${chrono_batch_render_origin}"
        )
    endif()
    if(chrono_batch_render_viewer)
        set_target_properties(
            chrono_batch_render_viewer
            PROPERTIES
                INSTALL_RPATH
                    "${chrono_batch_render_origin};${chrono_batch_render_origin}/chrono_batch_render"
        )
    endif()
    if(TARGET chrono_batch_render_viewer_xr)
        set_target_properties(
            chrono_batch_render_viewer_xr
            PROPERTIES
                INSTALL_RPATH
                    "${chrono_batch_render_origin};${chrono_batch_render_origin}/chrono_batch_render"
        )
    endif()
    if(TARGET chrono_batch_render_cli)
        file(
            RELATIVE_PATH
            chrono_batch_render_cli_to_libdir
            "/prefix/${CMAKE_INSTALL_BINDIR}"
            "/prefix/${CMAKE_INSTALL_LIBDIR}"
        )
        set_target_properties(
            chrono_batch_render_cli
            PROPERTIES
                INSTALL_RPATH
                    "${chrono_batch_render_origin}/${chrono_batch_render_cli_to_libdir};${chrono_batch_render_origin}/${chrono_batch_render_cli_to_libdir}/chrono_batch_render"
        )
    endif()
endif()

get_property(chrono_batch_render_public_files GLOBAL PROPERTY CHRONO_BATCH_RENDER_PUBLIC_FILES)
foreach(file IN LISTS chrono_batch_render_public_files)
    get_filename_component(directory "${file}" DIRECTORY)
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/${file}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${directory}"
        COMPONENT dev
    )
endforeach()
install(
    FILES
        "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/export.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render"
    COMPONENT dev
)
install(
    FILES
        "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/defines.h"
        "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/typed.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render/scene"
    COMPONENT dev
)
install(
    DIRECTORY
        "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/schema/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render/scene/schema"
    COMPONENT dev
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "columns.h"
        EXCLUDE
)
set(chrono_batch_render_dlpack_bundled OFF)
set(chrono_batch_render_pc_dlpack "")
if(NOT CHRONO_BATCH_RENDER_DLPACK_TARGET)
    set(chrono_batch_render_dlpack_bundled ON)
    set(chrono_batch_render_pc_dlpack "-I\${includedir}/chrono_batch_render/third_party/dlpack")
    install(
        FILES
            "${dlpack_SOURCE_DIR}/include/dlpack/dlpack.h"
        DESTINATION "${chrono_batch_render_third_party_dir}/dlpack/dlpack"
        COMPONENT dev
    )
endif()
set(chrono_batch_render_pc_imgui "")
if(chrono_batch_render_viewer)
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/chrono_batch_render/app/viewer/viewer.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render/app/viewer"
        COMPONENT viewer_dev
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/chrono_batch_render/app/viewer/imgui_config.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render/app/viewer"
        COMPONENT viewer_dev
    )
    install(
        FILES
            "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/viewer_export.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chrono_batch_render"
        COMPONENT viewer_dev
    )
    FetchContent_GetProperties(dear_imgui)
    FetchContent_GetProperties(glfw)
    FetchContent_GetProperties(lucide_license)
    if(NOT CHRONO_BATCH_RENDER_IMGUI_TARGET)
        file(
            WRITE "${PROJECT_BINARY_DIR}/imgui/imconfig.h"
            "// Dear ImGui's configuration as installed with Chrono Batch Render: its API is the viewer library's.\n"
            "#pragma once\n\n#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS\n"
            "#include \"chrono_batch_render/viewer_export.h\"\n#define IMGUI_API CHRONO_BATCH_RENDER_VIEWER_API\n"
        )
        install(
            FILES
                "${dear_imgui_SOURCE_DIR}/imgui.h"
                "${PROJECT_BINARY_DIR}/imgui/imconfig.h"
                "${dear_imgui_SOURCE_DIR}/imgui_internal.h"
                "${dear_imgui_SOURCE_DIR}/imstb_rectpack.h"
                "${dear_imgui_SOURCE_DIR}/imstb_textedit.h"
                "${dear_imgui_SOURCE_DIR}/imstb_truetype.h"
                "${dear_imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h"
            DESTINATION "${chrono_batch_render_third_party_dir}/imgui"
            COMPONENT viewer_dev
        )
        set(chrono_batch_render_pc_imgui "-I\${includedir}/chrono_batch_render/third_party/imgui")
    endif()
endif()

install(
    EXPORT chrono_batch_renderTargets
    NAMESPACE chrono_batch_render::
    DESTINATION "${chrono_batch_render_cmake_dir}"
    COMPONENT dev
)
if(chrono_batch_render_viewer)
    install(
        EXPORT chrono_batch_renderViewerTargets
        NAMESPACE chrono_batch_render::
        DESTINATION "${chrono_batch_render_cmake_dir}"
        COMPONENT viewer_dev
    )
endif()
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/chrono_batch_renderConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/chrono_batch_renderConfig.cmake"
    INSTALL_DESTINATION "${chrono_batch_render_cmake_dir}"
)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/chrono_batch_renderConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion
)
install(
    FILES
        "${PROJECT_BINARY_DIR}/chrono_batch_renderConfig.cmake"
        "${PROJECT_BINARY_DIR}/chrono_batch_renderConfigVersion.cmake"
    DESTINATION "${chrono_batch_render_cmake_dir}"
    COMPONENT dev
)
file(
    RELATIVE_PATH
    chrono_batch_render_pc_to_prefix
    "/prefix/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    "/prefix"
)
set(chrono_batch_render_pc_name chrono_batch_render)
set(chrono_batch_render_pc_library chrono_batch_render)
set(chrono_batch_render_pc_description "${PROJECT_DESCRIPTION}")
set(chrono_batch_render_pc_cflags "")
set(chrono_batch_render_pc_requires "")
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/chrono_batch_render.pc.in"
    "${PROJECT_BINARY_DIR}/chrono_batch_render.pc"
    @ONLY
)
install(
    FILES
        "${PROJECT_BINARY_DIR}/chrono_batch_render.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    COMPONENT dev
)
if(chrono_batch_render_viewer)
    set(chrono_batch_render_pc_name chrono_batch_render_viewer)
    set(chrono_batch_render_pc_library chrono_batch_render_viewer)
    set(chrono_batch_render_pc_description "The Chrono Batch Render viewer")
    set(chrono_batch_render_pc_cflags "${chrono_batch_render_pc_imgui}")
    set(chrono_batch_render_pc_requires chrono_batch_render)
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/chrono_batch_render.pc.in"
        "${PROJECT_BINARY_DIR}/chrono_batch_render_viewer.pc"
        @ONLY
    )
    install(
        FILES
            "${PROJECT_BINARY_DIR}/chrono_batch_render_viewer.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        COMPONENT viewer_dev
    )
endif()

set(chrono_batch_render_notices
    "slang|${slang_SOURCE_DIR}/LICENSE"
    "vulkan_headers|${vulkan_headers_SOURCE_DIR}/LICENSE.md"
    "volk|${volk_SOURCE_DIR}/LICENSE.md"
    "vma|${vma_SOURCE_DIR}/LICENSE.txt"
    "nvtx|${nvtx_SOURCE_DIR}/LICENSE.txt"
    "stb|${stb_SOURCE_DIR}/LICENSE"
    "openexr|${openexr_SOURCE_DIR}/LICENSE.md"
    "imath|${imath_SOURCE_DIR}/LICENSE.md"
    "libdeflate|${openexr_SOURCE_DIR}/external/deflate/COPYING"
    "zstd|${openexr_SOURCE_DIR}/external/zstd/LICENSE"
    "openjph|${openexr_SOURCE_DIR}/external/OpenJPH/LICENSE"
    "mikktspace|${mikktspace_SOURCE_DIR}/mikktspace.h"
    "tinyobjloader|${tinyobjloader_SOURCE_DIR}/LICENSE"
    "cgltf|${cgltf_SOURCE_DIR}/LICENSE"
    "tinyusd|${tinyusd_SOURCE_DIR}/LICENSE"
    "fmt|${fmt_SOURCE_DIR}/LICENSE"
)
if(chrono_batch_render_dlpack_bundled)
    list(APPEND chrono_batch_render_notices "dlpack|${dlpack_SOURCE_DIR}/LICENSE")
endif()
set(chrono_batch_render_viewer_notices)
if(chrono_batch_render_viewer)
    list(
        APPEND chrono_batch_render_viewer_notices
        "lucide|${lucide_license_SOURCE_DIR}/LICENSE"
        "glfw|${glfw_SOURCE_DIR}/LICENSE.md"
    )
    if(NOT CHRONO_BATCH_RENDER_IMGUI_TARGET)
        list(
            APPEND chrono_batch_render_viewer_notices
            "dear_imgui|${dear_imgui_SOURCE_DIR}/LICENSE.txt"
        )
    endif()
    if(TARGET chrono_batch_render_xr)
        FetchContent_GetProperties(openxr)
        list(APPEND chrono_batch_render_viewer_notices "openxr_sdk|${openxr_SOURCE_DIR}/LICENSE")
    endif()
endif()

install(
    FILES
        "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT runtime
)
file(READ "${PROJECT_SOURCE_DIR}/LICENSE" chrono_batch_render_license)
string(
    REGEX REPLACE "\n"
    "\n "
    chrono_batch_render_license
    " ${chrono_batch_render_license}"
)
set(chrono_batch_render_copyright
    "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\n"
    "Upstream-Name: Chrono Batch Render\nSource: ${PROJECT_HOMEPAGE_URL}\n\n"
    "Files: *\nCopyright: Alex Maietta\nLicense: BSD-3-Clause\n${chrono_batch_render_license}\n"
)
foreach(notice IN LISTS chrono_batch_render_notices chrono_batch_render_viewer_notices)
    string(
        REPLACE "|"
        ";"
        parts
        "${notice}"
    )
    list(GET parts 0 name)
    list(GET parts 1 file)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "No license for ${name} at ${file}")
    endif()
    get_filename_component(file_name "${file}" NAME)
    if(notice IN_LIST chrono_batch_render_viewer_notices)
        set(component viewer)
        set(python_component python)
        set(python_target _viewer)
    else()
        set(component runtime)
        set(python_component python)
        set(python_target _core)
    endif()
    install(
        FILES
            "${file}"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/third_party/${name}"
        COMPONENT ${component}
    )
    if(TARGET ${python_target})
        install(
            FILES
                "${file}"
            DESTINATION "chrono_batch_render/licenses/${name}"
            COMPONENT ${python_component}
        )
    endif()
    list(
        APPEND chrono_batch_render_copyright
        "\nFiles: bundled/${name}/*\nCopyright: see the license\n"
        "License: see /usr/${CMAKE_INSTALL_DOCDIR}/third_party/${name}/${file_name}\n"
    )
endforeach()
list(JOIN chrono_batch_render_copyright "" chrono_batch_render_copyright)
file(WRITE "${PROJECT_BINARY_DIR}/copyright" "${chrono_batch_render_copyright}")
install(
    FILES
        "${PROJECT_BINARY_DIR}/copyright"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT runtime
)
