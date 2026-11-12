# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_device
    buffer.cpp
    commands.cpp
    compute/runtime_compiler.cpp
    cuda/api.cpp
    cuda/adapters.cpp
    cuda/device.cpp
    device.cpp
    hip/api.cpp
    hip/adapters.cpp
    hip/device.cpp
    slang/compiler.cpp
    slang/debug.cpp
    program.cpp
    queue.cpp
    tensor.cpp
    vulkan/api.cpp
    vulkan/adapters.cpp
    vulkan/device.cpp
    vulkan/interop.cpp
    vulkan/native.cpp
    vulkan/queue.cpp
)
target_compile_definitions(chrono_batch_render_device PRIVATE VK_NO_PROTOTYPES)
target_link_libraries(
    chrono_batch_render_device
    PUBLIC
        chrono_batch_render_util
        chrono_batch_render_dlpack
    PRIVATE
        Vulkan::Headers
        volk::volk
        chrono_batch_render_vma
        chrono_batch_render_nvtx
        slang::slang
)

set(chrono_batch_render_prelude_inputs
    "${slang_SOURCE_DIR}/include/slang-cuda-prelude.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/compute/prelude.cuh"
    "${PROJECT_SOURCE_DIR}/cmake/ChronoBatchRenderPrelude.cmake"
)
foreach(backend IN ITEMS cuda hip)
    set(prelude "${CMAKE_CURRENT_BINARY_DIR}/${backend}_prelude.h")
    set(arguments
        "-DPRELUDE=${slang_SOURCE_DIR}/include/slang-cuda-prelude.h"
        "-DCOMPUTE_OVERLAY=${CMAKE_CURRENT_SOURCE_DIR}/compute/prelude.cuh"
        "-DOUTPUT=${prelude}"
    )
    set(inputs ${chrono_batch_render_prelude_inputs})
    if(backend STREQUAL "hip")
        list(APPEND arguments "-DHIP_OVERLAY=${CMAKE_CURRENT_SOURCE_DIR}/hip/prelude.hip")
        list(APPEND inputs "${CMAKE_CURRENT_SOURCE_DIR}/hip/prelude.hip")
    endif()
    add_custom_command(
        OUTPUT
            "${prelude}"
        COMMAND
            "${CMAKE_COMMAND}" ${arguments} -P
            "${PROJECT_SOURCE_DIR}/cmake/ChronoBatchRenderPrelude.cmake"
        DEPENDS
            ${inputs}
        VERBATIM
    )
    chrono_batch_render_embed(
        chrono_batch_render_device
        FILE "${prelude}"
        SYMBOL chrono_batch_render_${backend}_prelude
    )
endforeach()
chrono_batch_render_embed(
    chrono_batch_render_device
    LIBRARY kDeviceKernels
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/device/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernels|chrono_batch_render/device/kernels/|*.h"
)

chrono_batch_render_static_library(
    chrono_batch_render_device_internal
    chrono_batch_render_device
    chrono_batch_render_util_internal
    volk::volk
    chrono_batch_render_vma
    chrono_batch_render_nvtx
    slang::slang
)
target_compile_definitions(chrono_batch_render_device_internal INTERFACE VK_NO_PROTOTYPES)

chrono_batch_render_public_files(
    adapter.h
    device.h
    kernels/shared.h
    operation.h
    ref.h
    stream.h
    tensor.h
    vulkan/native.h
)
