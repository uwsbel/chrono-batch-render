# SPDX-License-Identifier: BSD-3-Clause
if(CHRONO_BATCH_RENDER_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(NOT CHRONO_BATCH_RENDER_BUILD_TESTS)
    return()
endif()

function(chrono_batch_render_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(
        ${name}
        PRIVATE
            ${ARG_LINK}
            GTest::gtest_main
    )
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    chrono_batch_render_warnings(${name})
    chrono_batch_render_coverage(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

chrono_batch_render_test(
    chrono_batch_render_app_tests
    LINK
        chrono_batch_render_app
    SOURCES
        unit/app/utest_app_cli.cpp
)

chrono_batch_render_test(
    chrono_batch_render_graph_tests
    LINK
        chrono_batch_render_graph_internal
    SOURCES
        unit/graph/utest_graph_graph.cpp
        unit/graph/utest_graph_library.cpp
)

chrono_batch_render_test(
    chrono_batch_render_sensor_pipeline_tests
    LINK
        chrono_batch_render_sensor_pipeline_internal
        chrono_batch_render_render_internal
    SOURCES
        unit/sensor_pipeline/utest_sensor_pipeline_codegen.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_fusion.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_global.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_isp.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_nodes.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_noise.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_reduce.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_reflection.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_resolve.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_segments.cpp
        unit/sensor_pipeline/utest_sensor_pipeline_standalone.cpp
)

chrono_batch_render_test(
    chrono_batch_render_material_tests
    LINK
        chrono_batch_render_material_internal
    SOURCES
        unit/material/utest_material_codegen.cpp
        unit/material/utest_material_uplift.cpp
)

chrono_batch_render_test(
    chrono_batch_render_algorithms_tests
    LINK
        chrono_batch_render_algorithms_internal
    SOURCES
        unit/algorithms/utest_algorithms_radix_sort.cpp
        unit/algorithms/utest_algorithms_scan.cpp
)

chrono_batch_render_test(
    chrono_batch_render_bvh_tests
    LINK
        chrono_batch_render_bvh_internal
    SOURCES
        unit/bvh/utest_bvh_builder.cpp
        unit/bvh/utest_bvh_inspect.cpp
        unit/bvh/utest_bvh_lbvh.cpp
        unit/bvh/utest_bvh_reference.cpp
)

chrono_batch_render_test(
    chrono_batch_render_device_tests
    LINK
        chrono_batch_render_device_internal
    SOURCES
        unit/device/utest_device_buffer.cpp
        unit/device/utest_device_commands.cpp
        unit/device/utest_device_queue_threads.cpp
        unit/device/utest_device_creator.cpp
        unit/device/utest_device_device.cpp
        unit/device/utest_device_hip.cpp
        unit/device/utest_device_program.cpp
        unit/device/utest_device_ref.cpp
        unit/device/utest_device_shared.cpp
        unit/device/utest_device_tensor.cpp
)

chrono_batch_render_test(
    chrono_batch_render_import_tests
    LINK
        chrono_batch_render_import_internal
    SOURCES
        unit/import/utest_import_importer.cpp
)

chrono_batch_render_test(
    chrono_batch_render_io_tests
    LINK
        chrono_batch_render_io_internal
    SOURCES
        unit/io/utest_io_encoding.cpp
        unit/io/utest_io_ies.cpp
        unit/io/utest_io_image.cpp
)

chrono_batch_render_test(
    chrono_batch_render_render_tests
    LINK
        chrono_batch_render_render_internal
    SOURCES
        unit/render/utest_render_camera.cpp
        unit/render/utest_render_detector.cpp
        unit/render/utest_render_emva1288.cpp
        unit/render/utest_render_in_flight.cpp
        unit/render/utest_render_threaded_writes.cpp
        unit/render/utest_render_lidar.cpp
        unit/render/utest_render_lidar_pipeline.cpp
        unit/render/utest_render_radar_pipeline.cpp
        unit/render/utest_render_origin.cpp
        unit/render/utest_render_overlay.cpp
        unit/render/utest_render_sensor_pipelines.cpp
        unit/render/utest_render_ray_cast.cpp
        unit/render/utest_render_ray_origin.cpp
        unit/render/utest_render_materials.cpp
        unit/render/utest_render_nodes.cpp
        unit/render/utest_render_planetary.cpp
        unit/render/utest_render_spectra.cpp
        unit/render/utest_render_products.cpp
        unit/render/utest_render_renderer.cpp
        unit/render/utest_render_meshes.cpp
        unit/render/utest_render_surfaces.cpp
)

chrono_batch_render_test(
    chrono_batch_render_scene_tests
    LINK
        chrono_batch_render_scene_internal
    SOURCES
        unit/scene/utest_scene_access.cpp
        unit/scene/utest_scene_attachment.cpp
        unit/scene/utest_scene_bounds.cpp
        unit/scene/utest_scene_lock.cpp
        unit/scene/utest_scene_follow.cpp
        unit/scene/utest_scene_structure.cpp
        unit/scene/utest_scene_meshes.cpp
        unit/scene/utest_scene_points.cpp
        unit/scene/utest_scene_transforms.cpp
        unit/scene/utest_scene_world_poses.cpp
        unit/scene/utest_scene_instancers.cpp
        unit/scene/utest_scene_instances.cpp
        unit/scene/utest_scene_lights.cpp
        unit/scene/utest_scene_move.cpp
        unit/scene/utest_scene_paths.cpp
        unit/scene/utest_scene_rotation.cpp
        unit/scene/utest_scene_runs.cpp
        unit/scene/utest_scene_scene.cpp
        unit/scene/utest_scene_semantics.cpp
        unit/scene/utest_scene_sensors.cpp
        unit/scene/utest_scene_snapshots.cpp
        unit/scene/utest_scene_column_store.cpp
        unit/scene/utest_scene_connections.cpp
        unit/scene/utest_scene_curves.cpp
        unit/scene/utest_scene_materials.cpp
        unit/scene/utest_scene_runtime_schemas.cpp
        unit/scene/utest_scene_tables.cpp
        unit/scene/utest_scene_tangents.cpp
        unit/scene/utest_scene_textures.cpp
        unit/scene/utest_scene_publisher.cpp
        unit/scene/utest_scene_sensor_pipelines.cpp
        unit/scene/utest_scene_products.cpp
        unit/scene/utest_scene_tokens.cpp
        unit/scene/utest_scene_updates.cpp
        unit/scene/utest_scene_views.cpp
)

chrono_batch_render_test(
    chrono_batch_render_texture_tests
    LINK
        chrono_batch_render_texture_internal
    SOURCES
        unit/texture/utest_texture_cache.cpp
)

chrono_batch_render_test(
    chrono_batch_render_util_tests
    LINK
        chrono_batch_render_util_internal
        chrono_batch_render_nvtx
    SOURCES
        unit/util/utest_util_disk_cache.cpp
        unit/util/utest_util_environment.cpp
        unit/util/utest_util_error.cpp
        unit/util/utest_util_library.cpp
        unit/util/utest_util_log.cpp
        unit/util/utest_util_profiler.cpp
        unit/util/utest_util_roctx.cpp
        unit/util/utest_util_span.cpp
        unit/util/utest_util_text.cpp
        unit/util/utest_util_thread_pool.cpp
        unit/util/utest_util_version.cpp
)
target_compile_definitions(
    chrono_batch_render_util_tests
    PRIVATE
        CHRONO_BATCH_RENDER_TEST_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        CHRONO_BATCH_RENDER_TEST_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        CHRONO_BATCH_RENDER_TEST_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        CHRONO_BATCH_RENDER_TEST_VERSION_FULL="${PROJECT_VERSION}${CHRONO_BATCH_RENDER_PRERELEASE}"
)

if(UNIX AND NOT APPLE)
    add_library(chrono_batch_render_fake_roctx SHARED support/fake_roctx.cpp)
    set_target_properties(
        chrono_batch_render_fake_roctx
        PROPERTIES
            OUTPUT_NAME
                roctx64
    )
    add_dependencies(chrono_batch_render_util_tests chrono_batch_render_fake_roctx)
    add_test(
        NAME chrono_batch_render_util_roctx_tests
        COMMAND
            chrono_batch_render_util_tests --gtest_filter=RoctxTest.*
    )
    set_tests_properties(
        chrono_batch_render_util_roctx_tests
        PROPERTIES
            ENVIRONMENT
                "LD_PRELOAD=$<TARGET_FILE:chrono_batch_render_fake_roctx>"
    )

    add_library(chrono_batch_render_fake_beside SHARED support/fake_beside.cpp)
    set_target_properties(
        chrono_batch_render_fake_beside
        PROPERTIES
            OUTPUT_NAME
                chrono_batch_render_beside
            DEBUG_POSTFIX
                ""
            LIBRARY_OUTPUT_DIRECTORY
                "${CMAKE_CURRENT_BINARY_DIR}/beside"
    )
    add_dependencies(chrono_batch_render_util_tests chrono_batch_render_fake_beside)
    target_compile_definitions(
        chrono_batch_render_util_tests
        PRIVATE
            CHRONO_BATCH_RENDER_TEST_BESIDE_DIR="$<TARGET_FILE_DIR:chrono_batch_render_fake_beside>"
    )
endif()

chrono_batch_render_test(
    chrono_batch_render_runtime_compiler_tests
    LINK
        chrono_batch_render_device_internal
        chrono_batch_render_render_internal
        chrono_batch_render_io_internal
        chrono_batch_render_material_internal
    SOURCES
        unit/device/utest_device_runtime_compilers.cpp
)
set_tests_properties(
    chrono_batch_render_runtime_compiler_tests
    PROPERTIES
        LABELS
            kernels
)

if(CHRONO_BATCH_RENDER_BUILD_VIEWER)
    chrono_batch_render_test(
        chrono_batch_render_viewer_tests
        LINK
            chrono_batch_render_viewer_internal
        SOURCES
            unit/app/viewer/utest_viewer_editor.cpp
            unit/app/viewer/utest_viewer_gizmo.cpp
            unit/app/viewer/utest_viewer_helpers.cpp
            unit/app/viewer/utest_viewer_history.cpp
            unit/app/viewer/utest_viewer_offscreen.cpp
            unit/app/viewer/utest_viewer_orbit_camera.cpp
            unit/app/viewer/utest_viewer_viewport.cpp
    )
    if(CHRONO_BATCH_RENDER_BUILD_XR)
        chrono_batch_render_test(
            chrono_batch_render_viewer_xr_tests
            LINK
                chrono_batch_render_viewer_xr
                chrono_batch_render_viewer
                chrono_batch_render
                Vulkan::Headers
            SOURCES
                unit/app/viewer/utest_viewer_xr.cpp
        )
        target_compile_definitions(
            chrono_batch_render_viewer_xr_tests
            PRIVATE
                VK_NO_PROTOTYPES
                GLFW_INCLUDE_NONE
        )
    endif()
endif()

if(CHRONO_BATCH_RENDER_BUILD_XR)
    chrono_batch_render_test(
        chrono_batch_render_xr_tests
        LINK
            chrono_batch_render_xr_internal
        SOURCES
            unit/xr/utest_xr_anchors.cpp
            unit/xr/utest_xr_locomotion.cpp
            unit/xr/utest_xr_openxr.cpp
            unit/xr/utest_xr_pointing.cpp
            unit/xr/utest_xr_recording.cpp
            unit/xr/utest_xr_rig.cpp
            unit/xr/utest_xr_session.cpp
    )
endif()

if(CHRONO_BATCH_RENDER_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter REQUIRED)
    set(chrono_batch_render_python_coverage)
    if(CHRONO_BATCH_RENDER_COVERAGE)
        set(chrono_batch_render_python_coverage
            --cov=chrono_batch_render
            --cov-report=term
            "--cov-report=xml:${PROJECT_BINARY_DIR}/python-coverage.xml"
        )
    endif()
    add_test(
        NAME chrono_batch_render_python_tests
        COMMAND
            "${Python_EXECUTABLE}" -m pytest "${CMAKE_CURRENT_SOURCE_DIR}/python" -q -rs -p
            no:cacheprovider ${chrono_batch_render_python_coverage}
    )
    set_tests_properties(
        chrono_batch_render_python_tests
        PROPERTIES
            ENVIRONMENT
                "PYTHONPATH=${PROJECT_BINARY_DIR}/python"
    )
endif()

add_test(
    NAME chrono_batch_render_schema_rejects_derived
    COMMAND
        "${CMAKE_COMMAND}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/schema/rejects_derived"
        "-DBINARY=${CMAKE_CURRENT_BINARY_DIR}/schema/rejects_derived" "-DROOT=${PROJECT_SOURCE_DIR}"
        "-DEXPECTED=chrono:computed is derived" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/schema/expect_configure_failure.cmake"
)
