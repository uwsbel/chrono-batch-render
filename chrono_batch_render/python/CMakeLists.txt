# SPDX-License-Identifier: BSD-3-Clause
include("${PROJECT_SOURCE_DIR}/cmake/third_party/nanobind.cmake")

set(chrono_batch_render_python_root "${PROJECT_BINARY_DIR}/python")
set(chrono_batch_render_python_dir "${chrono_batch_render_python_root}/chrono_batch_render")
set(chrono_batch_render_python_files
    __init__.py
    __main__.py
    _sensor_pipelines.py
    _typed.py
    py.typed
)

nanobind_add_module(
    _core
    STABLE_ABI
    NB_STATIC
    src/device.cpp
    src/dlpack.cpp
    src/importer.cpp
    src/io.cpp
    src/module.cpp
    src/sensor_pipeline.cpp
    src/render.cpp
    src/rotation.cpp
    src/scene.cpp
    src/values.cpp
    "${chrono_batch_render_python_defines}"
)
target_link_libraries(
    _core
    PRIVATE
        chrono_batch_render
        chrono_batch_render_app
        ${CMAKE_DL_LIBS}
)
chrono_batch_render_warnings(_core)
if(MSVC)
    target_compile_options(_core PRIVATE /wd4714)
endif()
foreach(nanobind IN ITEMS nanobind-static nanobind-static-abi3)
    if(TARGET ${nanobind})
        set_target_properties(
            ${nanobind}
            PROPERTIES
                SYSTEM
                    ON
        )
    endif()
endforeach()
chrono_batch_render_coverage(_core)
target_include_directories(_core PRIVATE src)
target_include_directories(_core SYSTEM PRIVATE ${Python_INCLUDE_DIRS})
set(chrono_batch_render_python_version "${PROJECT_VERSION}${CHRONO_BATCH_RENDER_PRERELEASE}")
if(DEFINED SKBUILD_PROJECT_VERSION_FULL)
    set(chrono_batch_render_python_version "${SKBUILD_PROJECT_VERSION_FULL}")
endif()
target_compile_definitions(
    _core
    PRIVATE
        CHRONO_BATCH_RENDER_VERSION="${chrono_batch_render_python_version}"
)
set_target_properties(
    _core
    PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
            "$<1:${chrono_batch_render_python_dir}>"
        DEBUG_POSTFIX
            ""
        INSTALL_RPATH
            "$<IF:$<PLATFORM_ID:Darwin>,@loader_path,$ORIGIN>"
)
if(WIN32)
    add_custom_command(
        TARGET _core
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:chrono_batch_render>"
            ${CHRONO_BATCH_RENDER_SLANG_RUNTIME} "${chrono_batch_render_python_dir}"
    )
endif()

foreach(file IN LISTS chrono_batch_render_python_files)
    configure_file(
        "chrono_batch_render/${file}"
        "${chrono_batch_render_python_dir}/${file}"
        COPYONLY
    )
endforeach()

nanobind_add_stub(
    chrono_batch_render_python_stubs_core
    MODULE chrono_batch_render._core
    OUTPUT "${chrono_batch_render_python_dir}/_core.pyi"
    PYTHON_PATH "${chrono_batch_render_python_root}"
    DEPENDS _core
)

install(
    TARGETS
        _core
    LIBRARY
        DESTINATION chrono_batch_render
        COMPONENT python
)
list(
    TRANSFORM chrono_batch_render_python_files
    PREPEND "chrono_batch_render/"
    OUTPUT_VARIABLE chrono_batch_render_python_sources
)
install(
    FILES
        ${chrono_batch_render_python_sources}
        "${chrono_batch_render_python_dir}/schemas.py"
        "${chrono_batch_render_python_dir}/_core.pyi"
    DESTINATION chrono_batch_render
    COMPONENT python
)
if(SKBUILD)
    set_property(TARGET chrono_batch_render PROPERTY VERSION)
    set_property(TARGET chrono_batch_render PROPERTY SOVERSION)
endif()
install(
    TARGETS
        chrono_batch_render
    RUNTIME
        DESTINATION chrono_batch_render
        COMPONENT python
    LIBRARY
        DESTINATION chrono_batch_render
        COMPONENT python
        NAMELINK_SKIP
)
install(
    PROGRAMS
        ${CHRONO_BATCH_RENDER_SLANG_RUNTIME}
    DESTINATION chrono_batch_render
    COMPONENT python
)

if(CHRONO_BATCH_RENDER_BUILD_VIEWER)
    nanobind_add_module(
        _viewer
        STABLE_ABI
        NB_STATIC
        src/imgui.cpp
        src/viewer.cpp
    )
    target_link_libraries(
        _viewer
        PRIVATE
            chrono_batch_render_viewer
            chrono_batch_render
    )
    chrono_batch_render_warnings(_viewer)
    if(MSVC)
        target_compile_options(_viewer PRIVATE /wd4714)
    endif()
    target_include_directories(_viewer PRIVATE src)
    target_include_directories(_viewer SYSTEM PRIVATE ${Python_INCLUDE_DIRS})
    set_target_properties(
        _viewer
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY
                "$<1:${chrono_batch_render_python_dir}>"
            DEBUG_POSTFIX
                ""
            INSTALL_RPATH
                "$<IF:$<PLATFORM_ID:Darwin>,@loader_path,$ORIGIN>"
    )
    if(WIN32)
        set(chrono_batch_render_viewer_dlls "$<TARGET_FILE:chrono_batch_render_viewer>")
        if(CHRONO_BATCH_RENDER_BUILD_XR)
            add_dependencies(_viewer chrono_batch_render_viewer_xr)
            list(
                APPEND chrono_batch_render_viewer_dlls
                "$<TARGET_FILE:chrono_batch_render_viewer_xr>"
                "$<TARGET_FILE:chrono_batch_render_xr>"
            )
        endif()
        add_custom_command(
            TARGET _viewer
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different ${chrono_batch_render_viewer_dlls}
                "${chrono_batch_render_python_dir}"
        )
    endif()
    configure_file(
        chrono_batch_render/viewer.py
        "${chrono_batch_render_python_dir}/viewer.py"
        COPYONLY
    )
    nanobind_add_stub(
        chrono_batch_render_python_stubs_viewer
        MODULE chrono_batch_render._viewer
        OUTPUT "${chrono_batch_render_python_dir}/_viewer.pyi"
        PYTHON_PATH "${chrono_batch_render_python_root}"
        DEPENDS
            _viewer
            _core
    )
    install(
        TARGETS
            _viewer
        LIBRARY
            DESTINATION chrono_batch_render
            COMPONENT python
    )
    install(
        FILES
            chrono_batch_render/viewer.py
            "${chrono_batch_render_python_dir}/_viewer.pyi"
        DESTINATION chrono_batch_render
        COMPONENT python
    )
endif()

if(CHRONO_BATCH_RENDER_BUILD_XR)
    nanobind_add_module(
        _xr
        STABLE_ABI
        NB_STATIC
        src/xr.cpp
    )
    target_link_libraries(
        _xr
        PRIVATE
            chrono_batch_render_xr
            chrono_batch_render
    )
    chrono_batch_render_warnings(_xr)
    target_include_directories(_xr PRIVATE src)
    target_include_directories(_xr SYSTEM PRIVATE ${Python_INCLUDE_DIRS})
    set_target_properties(
        _xr
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY
                "$<1:${chrono_batch_render_python_dir}>"
            DEBUG_POSTFIX
                ""
            INSTALL_RPATH
                "$<IF:$<PLATFORM_ID:Darwin>,@loader_path,$ORIGIN>"
    )
    if(WIN32)
        add_custom_command(
            TARGET _xr
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:chrono_batch_render_xr>"
                "${chrono_batch_render_python_dir}"
        )
    endif()
    configure_file(chrono_batch_render/xr.py "${chrono_batch_render_python_dir}/xr.py" COPYONLY)
    nanobind_add_stub(
        chrono_batch_render_python_stubs_xr
        MODULE chrono_batch_render._xr
        OUTPUT "${chrono_batch_render_python_dir}/_xr.pyi"
        PYTHON_PATH "${chrono_batch_render_python_root}"
        DEPENDS
            _xr
            _core
    )
    install(
        TARGETS
            _xr
        LIBRARY
            DESTINATION chrono_batch_render
            COMPONENT python
    )
    install(
        FILES
            chrono_batch_render/xr.py
            "${chrono_batch_render_python_dir}/_xr.pyi"
        DESTINATION chrono_batch_render
        COMPONENT python
    )
endif()
