# SPDX-License-Identifier: BSD-3-Clause
add_library(
    chrono_batch_render_bench_main
    STATIC
    main.cpp
    bench.cpp
)
chrono_batch_render_warnings(chrono_batch_render_bench_main)
target_link_libraries(
    chrono_batch_render_bench_main
    PUBLIC
        chrono_batch_render_device_internal
        benchmark::benchmark
)
target_include_directories(
    chrono_batch_render_bench_main
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/tests"
)
target_compile_definitions(
    chrono_batch_render_bench_main
    PRIVATE
        CHRONO_BATCH_RENDER_BENCH_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        CHRONO_BATCH_RENDER_BENCH_BUILD_TYPE="$<CONFIG>"
)

function(chrono_batch_render_bench name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")
    add_executable(${name} ${ARG_SOURCES})
    chrono_batch_render_warnings(${name})
    target_link_libraries(
        ${name}
        PRIVATE
            ${ARG_LINK}
            chrono_batch_render_bench_main
    )
    add_test(
        NAME ${name}
        COMMAND
            ${name} --benchmark_dry_run
    )
    set_tests_properties(
        ${name}
        PROPERTIES
            LABELS
                bench
            ENVIRONMENT
                CHRONO_BATCH_RENDER_LOG=warning
    )
endfunction()

chrono_batch_render_bench(
    chrono_batch_render_algorithms_bench
    LINK chrono_batch_render_algorithms_internal
    SOURCES
        algorithms/btest_algorithms_radix_sort.cpp
        algorithms/btest_algorithms_scan.cpp
)

chrono_batch_render_bench(
    chrono_batch_render_bvh_bench
    LINK chrono_batch_render_render_internal
    SOURCES
        bvh/btest_bvh_lbvh.cpp
        bvh/btest_bvh_trace.cpp
)

chrono_batch_render_bench(
    chrono_batch_render_device_bench
    LINK chrono_batch_render_device_internal
    SOURCES
        device/btest_device_tensor.cpp
)

chrono_batch_render_bench(
    chrono_batch_render_render_bench
    LINK chrono_batch_render_render_internal
    SOURCES
        render/btest_render_instancers.cpp
        render/btest_render_materials.cpp
        render/btest_render_overlay.cpp
        render/btest_render_curves.cpp
        render/btest_render_points.cpp
        render/btest_render_renderer.cpp
        render/btest_render_scaling.cpp
        render/btest_render_sensors.cpp
        render/btest_render_throughput.cpp
        render/btest_render_workloads.cpp
)

chrono_batch_render_bench(
    chrono_batch_render_scene_bench
    LINK chrono_batch_render_scene_internal
    SOURCES
        scene/btest_scene_publish.cpp
        scene/btest_scene_world_poses.cpp
)
