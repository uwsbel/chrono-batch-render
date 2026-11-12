// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/runtime/openxr.h"

namespace chrono::batch_render::xr::openxr {

void OpenXrRuntime::CreateSwapchains() {
    uint32_t count = 0;
    Check(xrEnumerateSwapchainFormats(m_session, 0, &count, nullptr),
          "xrEnumerateSwapchainFormats");
    std::vector<int64_t> formats(count);
    Check(xrEnumerateSwapchainFormats(m_session, count, &count, formats.data()),
          "xrEnumerateSwapchainFormats");
    const auto offered = [&](int64_t format) {
        return std::ranges::find(formats, format) != formats.end();
    };
    const int64_t format = offered(kSrgbFormat) ? kSrgbFormat : kUnormFormat;
    if (!offered(format)) {
        throw UnsupportedError("the OpenXR runtime offers no RGBA8 swapchain");
    }
    if (format == kUnormFormat) {
        Log(LogLevel::Warning,
            "the OpenXR runtime offers no sRGB swapchain, so colors show too bright");
    }
    for (EyeSwapchain& swapchain : m_swapchains) {
        swapchain = CreateSwapchain(format, XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                                                XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT);
    }
    if (m_depthLayer && !offered(kDepthFormat)) {
        Log(LogLevel::Warning,
            "the OpenXR runtime offers no D32 depth swapchain, so frames go without depth");
        m_depthLayer = false;
    }
    if (m_depthLayer) {
        for (EyeSwapchain& swapchain : m_depthSwapchains) {
            swapchain =
                CreateSwapchain(kDepthFormat, XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                                                  XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        }
    }
}

EyeSwapchain OpenXrRuntime::CreateSwapchain(int64_t format, XrSwapchainUsageFlags usage) {
    EyeSwapchain swapchain;
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags = usage;
    create.format = format;
    create.sampleCount = 1;
    create.width = m_swapchainSize.x;
    create.height = m_swapchainSize.y;
    create.faceCount = 1;
    create.arraySize = 1;
    create.mipCount = 1;
    Check(xrCreateSwapchain(m_session, &create, &swapchain.handle), "xrCreateSwapchain");
    uint32_t images = 0;
    Check(xrEnumerateSwapchainImages(swapchain.handle, 0, &images, nullptr),
          "xrEnumerateSwapchainImages");
    std::vector<XrSwapchainImageVulkan2KHR> vulkan(images, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
    Check(xrEnumerateSwapchainImages(swapchain.handle, images, &images,
                                     reinterpret_cast<XrSwapchainImageBaseHeader*>(vulkan.data())),
          "xrEnumerateSwapchainImages");
    for (const XrSwapchainImageVulkan2KHR& image : vulkan) {
        swapchain.images.push_back(image.image);
    }
    return swapchain;
}

void OpenXrRuntime::CreateCommands(const device::vulkan::Native& native) {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = native.queueFamily;
    if (m_vk.createCommandPool(native.device, &pool, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw Error("vkCreateCommandPool failed for the OpenXR swapchain copies");
    }
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = m_commandPool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = kCommandRing;
    VkCommandBuffer buffers[kCommandRing] = {};
    if (m_vk.allocateCommandBuffers(native.device, &allocate, buffers) != VK_SUCCESS) {
        throw Error("vkAllocateCommandBuffers failed for the OpenXR swapchain copies");
    }
    for (uint32_t i = 0; i < kCommandRing; ++i) {
        m_recordings[i].commands = buffers[i];
    }
}

device::Tensor OpenXrRuntime::Stage(const device::Tensor& source, device::Tensor& staged,
                                    std::vector<device::Operation>& after,
                                    const device::Operation& ready) {
    if (&source.GetDevice() == m_device.Get()) {
        return source;
    }
    const std::vector<int64_t> shape(source.Shape().begin(), source.Shape().end());
    if (staged.Empty() || !std::ranges::equal(staged.Shape(), shape)) {
        staged = m_device->CreateSharedTensor(source.Type(), shape, "xr staged eyes");
    }
    after.push_back(m_device->Copy(source, staged, ready));
    return staged;
}

void OpenXrRuntime::Transition(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                               VkImageLayout from, VkImageLayout to) const {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_MEMORY_WRITE_BIT
                                                              : VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_TRANSFER_WRITE_BIT
                                                              : VK_ACCESS_MEMORY_READ_BIT;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    m_vk.cmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                            &barrier);
}

void OpenXrRuntime::RecordCopy(VkCommandBuffer commands, const device::Tensor& eyes, VkImage image,
                               uint32_t eye, VkImageAspectFlags aspect, VkImageLayout final,
                               VkExtent2D rect) const {
    const uint32_t height = uint32_t(eyes.Shape()[1]);
    const uint32_t width = uint32_t(eyes.Shape()[2]);
    constexpr uint64_t texel = 4;
    Transition(commands, image, aspect, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = VkDeviceSize(eyes.Offset()) + VkDeviceSize(eye) * width * height * texel;
    copy.bufferRowLength = width;
    copy.bufferImageHeight = height;
    copy.imageSubresource = {aspect, 0, 0, 1};
    copy.imageExtent = {rect.width, rect.height, 1};
    m_vk.cmdCopyBufferToImage(commands, device::vulkan::NativeBuffer(eyes), image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    Transition(commands, image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, final);
}

std::array<uint32_t, 2> OpenXrRuntime::Acquire(const EyeSwapchain (&chains)[2]) const {
    CHRONO_BATCH_RENDER_ZONE("XR/Swapchain acquire");
    std::array<uint32_t, 2> indices{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        Check(xrAcquireSwapchainImage(chains[eye].handle, &acquire, &indices[eye]),
              "xrAcquireSwapchainImage");
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        Check(xrWaitSwapchainImage(chains[eye].handle, &wait), "xrWaitSwapchainImage");
    }
    return indices;
}

void OpenXrRuntime::Release(const EyeSwapchain (&chains)[2]) const {
    CHRONO_BATCH_RENDER_ZONE("XR/Swapchain release");
    for (uint32_t eye = 0; eye < 2; ++eye) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        XrResult result = XR_SUCCESS;
        device::vulkan::Exclusive(
            *m_device,
            [&](VkQueue) { result = xrReleaseSwapchainImage(chains[eye].handle, &release); },
            device::vulkan::QueueRole::Render);
        Check(result, "xrReleaseSwapchainImage");
    }
}

XrExtent2Di OpenXrRuntime::CopyToSwapchains(const EyeImages& eyes, bool withDepth) {
    CHRONO_BATCH_RENDER_ZONE("XR/Swapchain copy");
    const Span<const int64_t> shape = eyes.color.Shape();
    const VkExtent2D rect{std::min(uint32_t(shape[2]), m_swapchainSize.x),
                          std::min(uint32_t(shape[1]), m_swapchainSize.y)};
    Recording& recording = m_recordings[m_nextRecording];
    m_nextRecording = (m_nextRecording + 1) % kCommandRing;
    recording.done.Wait();
    std::vector<device::Operation> after = {eyes.ready};
    const device::Tensor color = Stage(eyes.color, recording.staged[0], after, eyes.ready);
    const device::Tensor depth =
        withDepth ? Stage(eyes.depth, recording.staged[1], after, eyes.ready) : device::Tensor();
    const std::array<uint32_t, 2> colorImages = Acquire(m_swapchains);
    std::array<uint32_t, 2> depthImages{};
    if (withDepth) {
        depthImages = Acquire(m_depthSwapchains);
    }
    VkCommandBuffer commands = recording.commands;
    m_vk.resetCommandBuffer(commands, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk.beginCommandBuffer(commands, &begin);
    for (uint32_t eye = 0; eye < 2; ++eye) {
        RecordCopy(commands, color, m_swapchains[eye].images[colorImages[eye]], eye,
                   VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, rect);
        if (withDepth) {
            RecordCopy(commands, depth, m_depthSwapchains[eye].images[depthImages[eye]], eye,
                       VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       rect);
        }
    }
    m_vk.endCommandBuffer(commands);
    recording.done = device::vulkan::Submit(
        *m_device, {.commands = {&commands, 1}, .after = after, .zone = "XR/Swapchain copy"},
        device::vulkan::QueueRole::Render);
    recording.kept[0] = color;
    recording.kept[1] = depth;
    Release(m_swapchains);
    if (withDepth) {
        Release(m_depthSwapchains);
    }
    return {int32_t(rect.width), int32_t(rect.height)};
}

}
