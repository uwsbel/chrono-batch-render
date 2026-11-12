// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <functional>

#include <vulkan/vulkan_core.h>

#ifndef VK_API_VERSION_1_3
#error "Chrono Batch Render's Vulkan interop needs the Vulkan 1.3 headers or later"
#endif

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/device/tensor.h"
#include "chrono_batch_render/export.h"
#include "chrono_batch_render/util/span.h"

namespace chrono::batch_render::device::vulkan {

enum class QueueRole : uint8_t { Render, Display };

struct Creator {
    std::function<VkInstance(PFN_vkGetInstanceProcAddr, const VkInstanceCreateInfo& info)>
        createInstance;
    std::function<VkPhysicalDevice(VkInstance instance)> pickAdapter;
    std::function<VkDevice(PFN_vkGetInstanceProcAddr, VkPhysicalDevice adapter,
                           const VkDeviceCreateInfo& info)>
        createDevice;
};

struct Native {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t apiVersion = 0;
    uint32_t queueFamily = 0;
    uint32_t queueIndex = 0;
    bool displaySharesRender = true;
};

struct NativeSubmit {
    Span<const VkCommandBuffer> commands;
    Span<const Operation> after;
    Span<const VkSemaphoreSubmitInfo> waits;
    Span<const VkSemaphoreSubmitInfo> signals;
    const char* zone = nullptr;
};

CHRONO_BATCH_RENDER_API Native NativeHandles(Device& device);

CHRONO_BATCH_RENDER_API VkBuffer NativeBuffer(const Tensor& tensor);

CHRONO_BATCH_RENDER_API Operation Submit(Device& device, const NativeSubmit& submit,
                                         QueueRole role = QueueRole::Render);

CHRONO_BATCH_RENDER_API VkResult Present(Device& device, const VkPresentInfoKHR& present);

CHRONO_BATCH_RENDER_API void Exclusive(Device& device,
                                       const std::function<void(VkQueue queue)>& use,
                                       QueueRole role = QueueRole::Display);

}
