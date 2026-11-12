// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#define XR_USE_GRAPHICS_API_VULKAN
#ifndef _WIN32
#include <time.h>
#define XR_USE_TIMESPEC
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "chrono_batch_render/device/vulkan/native.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/util/log.h"
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/runtime/profiles.h"
#include "chrono_batch_render/xr/runtime/runtime.h"

namespace chrono::batch_render::xr::openxr {

constexpr int64_t kSrgbFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr int64_t kUnormFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr int64_t kDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr XrViewConfigurationType kStereo = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr XrSpaceLocationFlags kLocated =
    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
constexpr uint32_t kCommandRing = 3;

const char* FeatureExtension(Feature feature);
const char* StateName(XrSessionState state);
void CopyName(char* destination, size_t size, std::string_view name);
Pose StagePose(const XrPosef& pose, bool valid);
Pose StagePose(const XrSpaceLocation& location);
std::string BindingPath(const ComponentBinding& binding);

struct EyeSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<VkImage> images;
};

struct HandSpaces {
    XrPath path = XR_NULL_PATH;
    XrSpace grip = XR_NULL_HANDLE;
    XrSpace aim = XR_NULL_HANDLE;
};

struct DeviceFunctions {
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
};

struct Recording {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    device::Operation done;
    device::Tensor staged[2];
    device::Tensor kept[2];
};

class OpenXrRuntime final : public Runtime {
  public:
    explicit OpenXrRuntime(const SessionDesc& desc);
    ~OpenXrRuntime() override;
    void ConfigureDevice(device::DeviceDesc& desc) override;
    void Start(device::Device& device) override;
    void Stop() override;

    bool Running() override {
        PollEvents();
        return !m_exit;
    }

    void BeginFrame(Frame& frame, FrameClock& clock) override;
    bool RelocateViews(Frame& frame) override;
    void EndFrame(const Frame& frame, const EyeImages* eyes) override;
    void RequestExit() override;

    bool DepthLayer() const override {
        return m_depthLayer;
    }

    BlendMode Blend() const override {
        return m_blend;
    }

    bool SetRefreshRate(float hz) override;

    Uint2 Recommended() const override {
        return m_recommended;
    }

    Uint2 Presented(Uint2) const override {
        return m_swapchainSize;
    }

    uint32_t Features() const override {
        return m_features;
    }

    void FillStatus(SessionStatus& status) const override;
    RuntimeMetrics Metrics(const SessionStats&) override;

  private:
    enum class Counter {
        AppCpu,
        AppGpu,
        AppLatency,
        CompositorCpu,
        CompositorGpu,
        Dropped,
        CpuUse,
        GpuUse
    };

    void Check(XrResult result, const char* call) const;
    void CreateInstance();
    void FindSystem();
    void ChooseBlendMode();

    template <class Function> void Load(const char* name, Function& function) {
        Check(xrGetInstanceProcAddr(m_instance, name,
                                    reinterpret_cast<PFN_xrVoidFunction*>(&function)),
              name);
    }

    void LoadFunctions();
    void LoadDeviceFunctions(const device::vulkan::Native& native);
    double DisplayHostTime(XrTime display, double woke, double period) const;

    bool Has(Feature feature) const {
        return (m_features >> uint32_t(feature) & 1u) != 0;
    }

    void CheckGraphicsRequirements();
    VkPhysicalDevice HeadsetAdapter(VkInstance instance);
    XrPath Path(const std::string& text) const;
    XrAction CreateAction(const char* name, const char* label, XrActionType type, bool hands);
    void CreateActions();

    void Suggest(const std::string& profile,
                 const std::vector<std::pair<XrAction, std::string>>& bindings);

    XrSpace CreateReferenceSpace(XrReferenceSpaceType type);
    XrSpace CreateActionSpace(XrAction action, XrPath hand);
    void CreateSpaces();
    void CreateSwapchains();
    EyeSwapchain CreateSwapchain(int64_t format, XrSwapchainUsageFlags usage);
    void CreateCommands(const device::vulkan::Native& native);

    device::Tensor Stage(const device::Tensor& source, device::Tensor& staged,
                         std::vector<device::Operation>& after, const device::Operation& ready);

    void Transition(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                    VkImageLayout from, VkImageLayout to) const;

    void RecordCopy(VkCommandBuffer commands, const device::Tensor& eyes, VkImage image,
                    uint32_t eye, VkImageAspectFlags aspect, VkImageLayout final,
                    VkExtent2D rect) const;

    std::array<uint32_t, 2> Acquire(const EyeSwapchain (&chains)[2]) const;
    void Release(const EyeSwapchain (&chains)[2]) const;
    XrExtent2Di CopyToSwapchains(const EyeImages& eyes, bool withDepth);
    void ReadRefreshRates();
    void CreateHandTrackers();
    void LocateHands(Frame& frame);
    void CreateBodyAndFaceTrackers();
    void LocateBody(Frame& frame);
    void ReadFace(Frame& frame);
    void StartMetrics();
    void ReadProfiles();
    void PollEvents();
    void ChangeState(XrSessionState state);
    void LocateViews(Frame& frame);
    Pose Locate(XrSpace space) const;
    XrActionStateGetInfo StateInfo(Slot slot, XrPath hand) const;
    float ReadFloat(Slot slot, XrPath hand) const;
    bool ReadBool(Slot slot, XrPath hand) const;
    bool PoseActive(XrAction action, XrPath hand) const;
    void ReadInputs(Frame& frame);
    void SendHaptics(const Frame& frame);

    SessionDesc m_desc;
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    std::vector<std::string> m_enabled;
    uint32_t m_features = 0;
    std::string m_runtimeName;
    std::string m_systemName;
    Uint2 m_recommended;
    Uint2 m_swapchainSize;

    PFN_xrGetVulkanGraphicsRequirements2KHR m_graphicsRequirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR m_createVulkanInstance = nullptr;
    PFN_xrCreateVulkanDeviceKHR m_createVulkanDevice = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR m_graphicsDevice = nullptr;
    PFN_xrEnumerateDisplayRefreshRatesFB m_enumerateRefreshRates = nullptr;
    PFN_xrGetDisplayRefreshRateFB m_getRefreshRate = nullptr;
    PFN_xrRequestDisplayRefreshRateFB m_requestRefreshRate = nullptr;
    PFN_xrCreateHandTrackerEXT m_createHandTracker = nullptr;
    PFN_xrDestroyHandTrackerEXT m_destroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT m_locateHandJoints = nullptr;
    PFN_xrCreateBodyTrackerFB m_createBodyTracker = nullptr;
    PFN_xrDestroyBodyTrackerFB m_destroyBodyTracker = nullptr;
    PFN_xrLocateBodyJointsFB m_locateBodyJoints = nullptr;
    PFN_xrCreateFaceTracker2FB m_createFaceTracker = nullptr;
    PFN_xrDestroyFaceTracker2FB m_destroyFaceTracker = nullptr;
    PFN_xrGetFaceExpressionWeights2FB m_faceWeights = nullptr;
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA m_enumerateCounters = nullptr;
    PFN_xrSetPerformanceMetricsStateMETA m_setMetricsState = nullptr;
    PFN_xrQueryPerformanceMetricsCounterMETA m_queryCounter = nullptr;
    bool m_graphicsChecked = false;
    bool m_hasTimespec = false;
#ifndef _WIN32
    PFN_xrConvertTimeToTimespecTimeKHR m_convertTime = nullptr;
#else
    void* m_convertTime = nullptr;
#endif

    XrActionSet m_actionSet = XR_NULL_HANDLE;
    std::array<XrAction, 10> m_actions{};
    XrAction m_gaze = XR_NULL_HANDLE;
    HandSpaces m_hands[2];

    Ref<device::Device> m_device;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    DeviceFunctions m_vk;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<Recording, kCommandRing> m_recordings;
    uint32_t m_nextRecording = 0;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_space = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;
    XrSpace m_gazeSpace = XR_NULL_HANDLE;
    EyeSwapchain m_swapchains[2];
    EyeSwapchain m_depthSwapchains[2];
    bool m_depthLayer = false;
    XrEnvironmentBlendMode m_blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    BlendMode m_blend = BlendMode::Opaque;
    XrHandTrackerEXT m_handTrackers[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrBodyTrackerFB m_bodyTracker = XR_NULL_HANDLE;
    XrFaceTracker2FB m_faceTracker = XR_NULL_HANDLE;
    std::vector<XrFaceTrackingDataSource2FB> m_faceSources;
    std::vector<std::pair<Counter, XrPath>> m_counters;
    std::string m_profiles[2];
    bool m_recentered = false;
    XrSessionState m_state = XR_SESSION_STATE_UNKNOWN;
    bool m_running = false;
    bool m_exit = false;
    bool m_frameBegun = false;
    XrTime m_displayTime = 0;
    XrTime m_lastDisplayTime = 0;
    XrView m_views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    bool m_viewsValid = false;
    float m_rate = 0;
    bool m_hasRefreshRate = false;
    std::vector<float> m_rates;
};

}
