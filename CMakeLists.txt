# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "")
project(
    chrono_batch_render
    VERSION 0.1.0
    DESCRIPTION
        "A physically-based, real-time, spectral renderer for planetary robotics and sensing"
    HOMEPAGE_URL "https://github.com/uwsbel/chrono-batch-render"
    LANGUAGES
        C
        CXX
)
set(CHRONO_BATCH_RENDER_PRERELEASE "")

option(CHRONO_BATCH_RENDER_BUILD_TESTS "Build Chrono Batch Render's tests" OFF)
option(CHRONO_BATCH_RENDER_BUILD_BENCHMARKS "Build Chrono Batch Render's benchmarks" OFF)
option(CHRONO_BATCH_RENDER_BUILD_PYTHON "Build Chrono Batch Render's Python bindings" OFF)
option(CHRONO_BATCH_RENDER_BUILD_VIEWER "Build the Chrono Batch Render viewer" OFF)
option(CHRONO_BATCH_RENDER_BUILD_XR "Build Chrono Batch Render's XR library" OFF)
option(CHRONO_BATCH_RENDER_INSTALL "Install Chrono Batch Render's library and headers" OFF)
option(
    CHRONO_BATCH_RENDER_COVERAGE
    "Instrument Chrono Batch Render for code coverage (GCC or Clang)"
    OFF
)
option(
    CHRONO_BATCH_RENDER_WARNINGS_AS_ERRORS
    "Fail on compiler and linker warnings in Chrono Batch Render's own targets"
    OFF
)
option(
    CHRONO_BATCH_RENDER_DEBIAN_LIBDIR
    "Install the libraries in Debian's lib/<arch>-linux-gnu"
    OFF
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32)
    add_compile_definitions(
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include")

include(FetchContent)
if(CHRONO_BATCH_RENDER_DEBIAN_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR
        "lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu"
        CACHE PATH
        "Object code libraries (LIBDIR)"
    )
endif()
include(GNUInstallDirs)

include(cmake/third_party/fmt.cmake)
include(cmake/third_party/vulkan_headers.cmake)
include(cmake/third_party/volk.cmake)
include(cmake/third_party/vma.cmake)
include(cmake/third_party/dlpack.cmake)
include(cmake/third_party/nvtx.cmake)
include(cmake/third_party/slang.cmake)
include(cmake/third_party/mikktspace.cmake)
include(cmake/third_party/stb.cmake)
include(cmake/third_party/fonts.cmake)
include(cmake/third_party/openexr.cmake)
include(cmake/third_party/tinyobjloader.cmake)
include(cmake/third_party/cgltf.cmake)
include(cmake/third_party/tinyusd.cmake)

add_subdirectory(chrono_batch_render)
if(CHRONO_BATCH_RENDER_BUILD_XR)
    add_subdirectory(chrono_batch_render/xr)
endif()
add_subdirectory(chrono_batch_render/app)

if(CHRONO_BATCH_RENDER_BUILD_TESTS OR CHRONO_BATCH_RENDER_BUILD_BENCHMARKS)
    enable_testing()
    if(CHRONO_BATCH_RENDER_BUILD_TESTS)
        include(cmake/third_party/google_test.cmake)
    endif()
    if(CHRONO_BATCH_RENDER_BUILD_BENCHMARKS)
        include(cmake/third_party/google_benchmark.cmake)
    endif()
    add_subdirectory(tests)
endif()

if(CHRONO_BATCH_RENDER_INSTALL)
    include(cmake/ChronoBatchRenderInstall.cmake)
    include(cmake/ChronoBatchRenderPackaging.cmake)
endif()
