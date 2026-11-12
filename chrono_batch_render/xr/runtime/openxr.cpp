// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/xr/runtime/openxr.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render::xr::openxr {

const char* FeatureExtension(Feature feature) {
    switch (feature) {
    case Feature::EyeGaze:
        return XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME;
    case Feature::HandTracking:
        return XR_EXT_HAND_TRACKING_EXTENSION_NAME;
    case Feature::DisplayRefreshRate:
        return XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    case Feature::PerformanceMetrics:
        return XR_META_PERFORMANCE_METRICS_EXTENSION_NAME;
    case Feature::HandInteraction:
        return XR_EXT_HAND_INTERACTION_EXTENSION_NAME;
    case Feature::BodyTracking:
        return XR_FB_BODY_TRACKING_EXTENSION_NAME;
    case Feature::FaceTracking:
        return XR_FB_FACE_TRACKING2_EXTENSION_NAME;
    }
    return "";
}

const char* StateName(XrSessionState state) {
    switch (state) {
    case XR_SESSION_STATE_IDLE:
        return "idle";
    case XR_SESSION_STATE_READY:
        return "ready";
    case XR_SESSION_STATE_SYNCHRONIZED:
        return "synchronized";
    case XR_SESSION_STATE_VISIBLE:
        return "visible";
    case XR_SESSION_STATE_FOCUSED:
        return "focused";
    case XR_SESSION_STATE_STOPPING:
        return "stopping";
    case XR_SESSION_STATE_LOSS_PENDING:
        return "loss pending";
    case XR_SESSION_STATE_EXITING:
        return "exiting";
    default:
        return "unknown";
    }
}

void CopyName(char* destination, size_t size, std::string_view name) {
    const size_t length = std::min(size - 1, name.size());
    std::memcpy(destination, name.data(), length);
    destination[length] = '\0';
}

Pose StagePose(const XrPosef& pose, bool valid) {
    const double half = std::sqrt(0.5);
    const Quatd orientation{pose.orientation.x, pose.orientation.y, pose.orientation.z,
                            pose.orientation.w};
    return {{pose.position.x, -pose.position.z, pose.position.y},
            Multiply({half, 0, 0, half}, orientation),
            valid};
}

Pose StagePose(const XrSpaceLocation& location) {
    return StagePose(location.pose, (location.locationFlags & kLocated) == kLocated);
}

std::string BindingPath(const ComponentBinding& binding) {
    const std::string hand = binding.hand == 0 ? "/user/hand/left" : "/user/hand/right";
    return binding.slot == Slot::Haptic ? hand + "/output/haptic"
                                        : hand + "/input/" + binding.component;
}

OpenXrRuntime::OpenXrRuntime(const SessionDesc& desc) : m_desc(desc) {
    CreateInstance();
    try {
        FindSystem();
        LoadFunctions();
        CreateActions();
    } catch (...) {
        xrDestroyInstance(m_instance);
        throw;
    }
}

OpenXrRuntime::~OpenXrRuntime() {
    Stop();
    if (m_actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(m_actionSet);
    }
    xrDestroyInstance(m_instance);
}

void OpenXrRuntime::ConfigureDevice(device::DeviceDesc& desc) {
    auto creator = std::make_shared<device::vulkan::Creator>();
    creator->createInstance = [this](PFN_vkGetInstanceProcAddr get,
                                     const VkInstanceCreateInfo& info) {
        CheckGraphicsRequirements();
        XrVulkanInstanceCreateInfoKHR create{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        create.systemId = m_system;
        create.pfnGetInstanceProcAddr = get;
        create.vulkanCreateInfo = &info;
        VkInstance instance = VK_NULL_HANDLE;
        VkResult result = VK_SUCCESS;
        Check(m_createVulkanInstance(m_instance, &create, &instance, &result),
              "xrCreateVulkanInstanceKHR");
        if (result != VK_SUCCESS) {
            throw Error("the OpenXR runtime's vkCreateInstance failed with VkResult {}",
                        int(result));
        }
        return instance;
    };
    creator->pickAdapter = [this](VkInstance instance) { return HeadsetAdapter(instance); };
    creator->createDevice = [this](PFN_vkGetInstanceProcAddr get, VkPhysicalDevice adapter,
                                   const VkDeviceCreateInfo& info) {
        XrVulkanDeviceCreateInfoKHR create{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        create.systemId = m_system;
        create.pfnGetInstanceProcAddr = get;
        create.vulkanPhysicalDevice = adapter;
        create.vulkanCreateInfo = &info;
        VkDevice device = VK_NULL_HANDLE;
        VkResult result = VK_SUCCESS;
        Check(m_createVulkanDevice(m_instance, &create, &device, &result),
              "xrCreateVulkanDeviceKHR");
        if (result != VK_SUCCESS) {
            throw Error("the OpenXR runtime's vkCreateDevice failed with VkResult {}", int(result));
        }
        return device;
    };
    desc.backend = device::Backend::Vulkan;
    desc.presentation = true;
    desc.creator = creator;
}

void OpenXrRuntime::Start(device::Device& device) {
    if (device.Adapter().backend != device::Backend::Vulkan || !device.Presents()) {
        throw Error("the OpenXR session needs the Vulkan device Session::CreateDevice makes");
    }
    const device::vulkan::Native native = device::vulkan::NativeHandles(device);
    CheckGraphicsRequirements();
    if (HeadsetAdapter(native.instance) != native.physicalDevice) {
        throw Error("the device isn't on the headset's adapter; make it with "
                    "Session::CreateDevice");
    }
    m_device = Ref<device::Device>(&device);
    LoadDeviceFunctions(native);
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = native.instance;
    binding.physicalDevice = native.physicalDevice;
    binding.device = native.device;
    binding.queueFamilyIndex = native.queueFamily;
    binding.queueIndex = native.queueIndex;
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};
    create.next = &binding;
    create.systemId = m_system;
    Check(xrCreateSession(m_instance, &create, &m_session), "xrCreateSession");
    CreateSpaces();
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &m_actionSet;
    Check(xrAttachSessionActionSets(m_session, &attach), "xrAttachSessionActionSets");
    CreateSwapchains();
    CreateCommands(native);
    ReadRefreshRates();
    CreateHandTrackers();
    CreateBodyAndFaceTrackers();
    StartMetrics();
}

void OpenXrRuntime::Stop() {
    for (Recording& recording : m_recordings) {
        recording.done.Wait();
        recording = {};
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        m_vk.destroyCommandPool(m_vkDevice, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    for (XrHandTrackerEXT& tracker : m_handTrackers) {
        if (tracker != XR_NULL_HANDLE) {
            m_destroyHandTracker(tracker);
            tracker = XR_NULL_HANDLE;
        }
    }
    if (m_bodyTracker != XR_NULL_HANDLE) {
        m_destroyBodyTracker(m_bodyTracker);
        m_bodyTracker = XR_NULL_HANDLE;
    }
    if (m_faceTracker != XR_NULL_HANDLE) {
        m_destroyFaceTracker(m_faceTracker);
        m_faceTracker = XR_NULL_HANDLE;
    }
    m_counters.clear();
    for (EyeSwapchain* chains : {m_swapchains, m_depthSwapchains}) {
        for (uint32_t eye = 0; eye < 2; ++eye) {
            if (chains[eye].handle != XR_NULL_HANDLE) {
                xrDestroySwapchain(chains[eye].handle);
            }
            chains[eye] = {};
        }
    }
    for (HandSpaces& hand : m_hands) {
        for (XrSpace* space : {&hand.grip, &hand.aim}) {
            if (*space != XR_NULL_HANDLE) {
                xrDestroySpace(*space);
                *space = XR_NULL_HANDLE;
            }
        }
    }
    for (XrSpace* space : {&m_gazeSpace, &m_viewSpace, &m_space}) {
        if (*space != XR_NULL_HANDLE) {
            xrDestroySpace(*space);
            *space = XR_NULL_HANDLE;
        }
    }
    if (m_session != XR_NULL_HANDLE) {
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }
    m_running = false;
    m_device = {};
}

void OpenXrRuntime::BeginFrame(Frame& frame, FrameClock& clock) {
    PollEvents();
    while (m_session != XR_NULL_HANDLE && !m_running && !m_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        PollEvents();
    }
    if (!m_running) {
        frame.shouldRender = false;
        frame.state = SessionState(std::min<uint32_t>(m_state, XR_SESSION_STATE_EXITING));
        return;
    }
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state{XR_TYPE_FRAME_STATE};
    {
        CHRONO_BATCH_RENDER_ZONE("XR/xrWaitFrame");
        Check(xrWaitFrame(m_session, &wait, &state), "xrWaitFrame");
    }
    const double woke = HostSeconds();
    clock.period = double(state.predictedDisplayPeriod) * 1e-9;
    if (m_lastDisplayTime != 0 && state.predictedDisplayPeriod > 0) {
        const int64_t skipped =
            (state.predictedDisplayTime - m_lastDisplayTime + state.predictedDisplayPeriod / 2) /
                state.predictedDisplayPeriod -
            1;
        clock.missed = uint32_t(std::max<int64_t>(skipped, 0));
    }
    clock.display = DisplayHostTime(state.predictedDisplayTime, woke, clock.period);
    clock.estimated = m_convertTime == nullptr;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    Check(xrBeginFrame(m_session, &begin), "xrBeginFrame");
    m_frameBegun = true;
    m_displayTime = state.predictedDisplayTime;
    if (!m_hasRefreshRate && state.predictedDisplayPeriod > 0) {
        m_rate = float(1e9 / double(state.predictedDisplayPeriod));
    }
    frame.displayTime = state.predictedDisplayTime;
    frame.deltaTime = double(m_lastDisplayTime != 0 ? state.predictedDisplayTime - m_lastDisplayTime
                                                    : state.predictedDisplayPeriod) *
                      1e-9;
    m_lastDisplayTime = state.predictedDisplayTime;
    frame.shouldRender = state.shouldRender == XR_TRUE;
    frame.state = SessionState(std::min<uint32_t>(m_state, XR_SESSION_STATE_EXITING));
    LocateViews(frame);
    clock.poses = HostSeconds();
    ReadInputs(frame);
}

void OpenXrRuntime::EndFrame(const Frame& frame, const EyeImages* eyes) {
    if (!m_frameBegun) {
        return;
    }
    m_frameBegun = false;
    XrCompositionLayerProjectionView views[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                 {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerDepthInfoKHR depths[2] = {{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR},
                                                {XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR}};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<const XrCompositionLayerBaseHeader*> layers;
    if (eyes != nullptr && frame.shouldRender && m_viewsValid) {
        const bool withDepth = m_depthLayer && !eyes->depth.Empty();
        const XrExtent2Di rect = CopyToSwapchains(*eyes, withDepth);
        for (uint32_t eye = 0; eye < 2; ++eye) {
            if (withDepth) {
                XrCompositionLayerDepthInfoKHR& info = depths[eye];
                info.subImage.swapchain = m_depthSwapchains[eye].handle;
                info.subImage.imageRect = {{0, 0}, rect};
                info.minDepth = 0;
                info.maxDepth = 1;
                info.nearZ = m_desc.nearPlane;
                info.farZ = m_desc.farPlane;
                views[eye].next = &info;
            }
            views[eye].pose = m_views[eye].pose;
            views[eye].fov = m_views[eye].fov;
            views[eye].subImage.swapchain = m_swapchains[eye].handle;
            views[eye].subImage.imageRect = {{0, 0}, rect};
        }
        layer.space = m_space;
        if (m_blendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                               XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        }
        layer.viewCount = 2;
        layer.views = views;
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer));
    }
    SendHaptics(frame);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = m_displayTime;
    end.environmentBlendMode = m_blendMode;
    end.layerCount = uint32_t(layers.size());
    end.layers = layers.data();
    XrResult result = XR_SUCCESS;
    {
        CHRONO_BATCH_RENDER_ZONE("XR/xrEndFrame");
        device::vulkan::Exclusive(
            *m_device, [&](VkQueue) { result = xrEndFrame(m_session, &end); },
            device::vulkan::QueueRole::Render);
    }
    Check(result, "xrEndFrame");
}

void OpenXrRuntime::RequestExit() {
    if (m_session != XR_NULL_HANDLE && m_running) {
        Check(xrRequestExitSession(m_session), "xrRequestExitSession");
    } else {
        m_exit = true;
    }
}

bool OpenXrRuntime::SetRefreshRate(float hz) {
    if (!m_hasRefreshRate || m_session == XR_NULL_HANDLE ||
        std::ranges::find(m_rates, hz) == m_rates.end()) {
        return false;
    }
    return XR_SUCCEEDED(m_requestRefreshRate(m_session, hz));
}

void OpenXrRuntime::FillStatus(SessionStatus& status) const {
    status.runtime = m_runtimeName;
    status.system = m_systemName;
    status.state = StateName(m_state);
    status.refreshRate = m_rate;
    status.refreshRates = m_rates;
}

RuntimeMetrics OpenXrRuntime::Metrics(const SessionStats&) {
    RuntimeMetrics metrics;
    if (m_counters.empty() || m_session == XR_NULL_HANDLE) {
        return metrics;
    }
    metrics.available = true;
    for (const auto& [counter, path] : m_counters) {
        XrPerformanceMetricsCounterMETA value{XR_TYPE_PERFORMANCE_METRICS_COUNTER_META};
        if (XR_FAILED(m_queryCounter(m_session, path, &value)) ||
            (value.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META) == 0) {
            continue;
        }
        const bool isFloat =
            (value.counterFlags & XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META) != 0;
        const double number = isFloat ? value.floatValue : double(value.uintValue);
        const double seconds =
            value.counterUnit == XR_PERFORMANCE_METRICS_COUNTER_UNIT_MILLISECONDS_META
                ? number * 1e-3
                : number;
        const float fraction = float(
            value.counterUnit == XR_PERFORMANCE_METRICS_COUNTER_UNIT_PERCENTAGE_META ? number / 100
                                                                                     : number);
        switch (counter) {
        case Counter::AppCpu:
            metrics.appCpu = seconds;
            break;
        case Counter::AppGpu:
            metrics.appGpu = seconds;
            break;
        case Counter::AppLatency:
            metrics.motionToPhoton = seconds;
            break;
        case Counter::CompositorCpu:
            metrics.compositorCpu = seconds;
            break;
        case Counter::CompositorGpu:
            metrics.compositorGpu = seconds;
            break;
        case Counter::Dropped:
            metrics.droppedFrames = uint32_t(number);
            break;
        case Counter::CpuUse:
            metrics.cpuUtilization = fraction;
            break;
        case Counter::GpuUse:
            metrics.gpuUtilization = fraction;
            break;
        }
    }
    return metrics;
}

void OpenXrRuntime::Check(XrResult result, const char* call) const {
    if (XR_SUCCEEDED(result)) {
        return;
    }
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (m_instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(m_instance, result, name))) {
        std::snprintf(name, sizeof(name), "XrResult %d", int(result));
    }
    throw Error("{} failed: {}", call, name);
}

void OpenXrRuntime::CreateInstance() {
    uint32_t count = 0;
    const XrResult listed = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    if (XR_FAILED(listed)) {
        throw UnsupportedError(
            "no OpenXR runtime is active (XrResult {}); start one, such as SteamVR, Meta "
            "Quest Link or Monado, or set CHRONO_BATCH_RENDER_XR=sim for the simulated headset",
            int(listed));
    }
    std::vector<XrExtensionProperties> offered(count, {XR_TYPE_EXTENSION_PROPERTIES});
    Check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, offered.data()),
          "xrEnumerateInstanceExtensionProperties");
    const auto has = [&](std::string_view name) {
        return std::ranges::any_of(offered, [&](const XrExtensionProperties& extension) {
            return name == extension.extensionName;
        });
    };
    if (!has(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
        throw UnsupportedError("the OpenXR runtime lacks {}", XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
    }
    const auto enable = [&](std::string_view name) {
        if (!has(name)) {
            return false;
        }
        if (std::ranges::find(m_enabled, name) == m_enabled.end()) {
            m_enabled.emplace_back(name);
        }
        return true;
    };
    enable(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
    std::vector<Feature> features = m_desc.features;
    if (m_desc.refreshRate > 0) {
        features.push_back(Feature::DisplayRefreshRate);
    }
    for (const Feature feature : features) {
        if (enable(FeatureExtension(feature))) {
            m_features |= 1u << uint32_t(feature);
        } else {
            Log(LogLevel::Info, "the OpenXR runtime lacks {}; going on without it",
                FeatureExtension(feature));
        }
    }
    if (m_desc.depthLayer) {
        m_depthLayer = enable(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
    }
    for (const InteractionProfile& profile : InteractionProfiles()) {
        if (!profile.extension.empty() && !profile.feature) {
            enable(profile.extension);
        }
    }
#ifndef _WIN32
    m_hasTimespec = enable(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME);
#endif
    std::vector<const char*> names;
    for (const std::string& name : m_enabled) {
        names.push_back(name.c_str());
    }
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    CopyName(create.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE,
             m_desc.application);
    CopyName(create.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Chrono Batch Render");
    create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    create.enabledExtensionCount = uint32_t(names.size());
    create.enabledExtensionNames = names.data();
    const XrResult created = xrCreateInstance(&create, &m_instance);
    if (created == XR_ERROR_RUNTIME_UNAVAILABLE || created == XR_ERROR_RUNTIME_FAILURE) {
        throw UnsupportedError(
            "the OpenXR runtime didn't start (XrResult {}); start its "
            "service, or set CHRONO_BATCH_RENDER_XR=sim for the simulated headset",
            int(created));
    }
    Check(created, "xrCreateInstance");
    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    Check(xrGetInstanceProperties(m_instance, &properties), "xrGetInstanceProperties");
    m_runtimeName = fmt::format(
        "{} {}.{}.{}", properties.runtimeName, XR_VERSION_MAJOR(properties.runtimeVersion),
        XR_VERSION_MINOR(properties.runtimeVersion), XR_VERSION_PATCH(properties.runtimeVersion));
}

void OpenXrRuntime::FindSystem() {
    XrSystemGetInfo get{XR_TYPE_SYSTEM_GET_INFO};
    get.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult result = xrGetSystem(m_instance, &get, &m_system);
    if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
        throw UnsupportedError("the OpenXR runtime {} has no headset connected", m_runtimeName);
    }
    Check(result, "xrGetSystem");
    XrSystemEyeGazeInteractionPropertiesEXT gaze{
        XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
    XrSystemHandTrackingPropertiesEXT hands{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    XrSystemBodyTrackingPropertiesFB body{XR_TYPE_SYSTEM_BODY_TRACKING_PROPERTIES_FB};
    XrSystemFaceTrackingProperties2FB face{XR_TYPE_SYSTEM_FACE_TRACKING_PROPERTIES2_FB};
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    if (Has(Feature::EyeGaze)) {
        gaze.next = properties.next;
        properties.next = &gaze;
    }
    if (Has(Feature::HandTracking)) {
        hands.next = properties.next;
        properties.next = &hands;
    }
    if (Has(Feature::BodyTracking)) {
        body.next = properties.next;
        properties.next = &body;
    }
    if (Has(Feature::FaceTracking)) {
        face.next = properties.next;
        properties.next = &face;
    }
    Check(xrGetSystemProperties(m_instance, m_system, &properties), "xrGetSystemProperties");
    m_systemName = properties.systemName;
    if (Has(Feature::EyeGaze) && gaze.supportsEyeGazeInteraction != XR_TRUE) {
        m_features &= ~(1u << uint32_t(Feature::EyeGaze));
    }
    if (Has(Feature::HandTracking) && hands.supportsHandTracking != XR_TRUE) {
        m_features &= ~(1u << uint32_t(Feature::HandTracking));
    }
    if (Has(Feature::BodyTracking) && body.supportsBodyTracking != XR_TRUE) {
        Log(LogLevel::Info, "the headset can't track the body; going on without it");
        m_features &= ~(1u << uint32_t(Feature::BodyTracking));
    }
    m_faceSources.clear();
    if (Has(Feature::FaceTracking)) {
        if (face.supportsVisualFaceTracking == XR_TRUE) {
            m_faceSources.push_back(XR_FACE_TRACKING_DATA_SOURCE2_VISUAL_FB);
        }
        if (face.supportsAudioFaceTracking == XR_TRUE) {
            m_faceSources.push_back(XR_FACE_TRACKING_DATA_SOURCE2_AUDIO_FB);
        }
        if (m_faceSources.empty()) {
            Log(LogLevel::Info, "the headset can't track the face; going on without it");
            m_features &= ~(1u << uint32_t(Feature::FaceTracking));
        }
    }
    uint32_t count = 0;
    Check(xrEnumerateViewConfigurationViews(m_instance, m_system, kStereo, 0, &count, nullptr),
          "xrEnumerateViewConfigurationViews");
    std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    Check(xrEnumerateViewConfigurationViews(m_instance, m_system, kStereo, count, &count,
                                            views.data()),
          "xrEnumerateViewConfigurationViews");
    if (count != 2) {
        throw UnsupportedError("the headset has no stereo view configuration");
    }
    m_recommended = {views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight};
    const float scale = std::clamp(m_desc.resolutionScale, 0.1f, 2.0f);
    m_swapchainSize = {std::clamp(uint32_t(std::lround(float(m_recommended.x) * scale)), 1u,
                                  views[0].maxImageRectWidth),
                       std::clamp(uint32_t(std::lround(float(m_recommended.y) * scale)), 1u,
                                  views[0].maxImageRectHeight)};
    ChooseBlendMode();
}

void OpenXrRuntime::ChooseBlendMode() {
    uint32_t count = 0;
    Check(xrEnumerateEnvironmentBlendModes(m_instance, m_system, kStereo, 0, &count, nullptr),
          "xrEnumerateEnvironmentBlendModes");
    std::vector<XrEnvironmentBlendMode> modes(count);
    Check(xrEnumerateEnvironmentBlendModes(m_instance, m_system, kStereo, count, &count,
                                           modes.data()),
          "xrEnumerateEnvironmentBlendModes");
    const XrEnvironmentBlendMode wanted =
        m_desc.blendMode == BlendMode::Additive     ? XR_ENVIRONMENT_BLEND_MODE_ADDITIVE
        : m_desc.blendMode == BlendMode::AlphaBlend ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
                                                    : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (std::ranges::find(modes, wanted) != modes.end()) {
        m_blendMode = wanted;
    } else {
        if (wanted != XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
            Log(LogLevel::Warning, "the headset doesn't offer that blend mode, so it's opaque");
        }
        m_blendMode = modes.empty() ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : modes.front();
    }
    m_blend = m_blendMode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE      ? BlendMode::Additive
              : m_blendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND ? BlendMode::AlphaBlend
                                                                     : BlendMode::Opaque;
}

void OpenXrRuntime::LoadFunctions() {
    Load("xrGetVulkanGraphicsRequirements2KHR", m_graphicsRequirements);
    Load("xrCreateVulkanInstanceKHR", m_createVulkanInstance);
    Load("xrCreateVulkanDeviceKHR", m_createVulkanDevice);
    Load("xrGetVulkanGraphicsDevice2KHR", m_graphicsDevice);
    if (Has(Feature::DisplayRefreshRate)) {
        Load("xrEnumerateDisplayRefreshRatesFB", m_enumerateRefreshRates);
        Load("xrGetDisplayRefreshRateFB", m_getRefreshRate);
        Load("xrRequestDisplayRefreshRateFB", m_requestRefreshRate);
    }
    if (Has(Feature::HandTracking)) {
        Load("xrCreateHandTrackerEXT", m_createHandTracker);
        Load("xrDestroyHandTrackerEXT", m_destroyHandTracker);
        Load("xrLocateHandJointsEXT", m_locateHandJoints);
    }
    if (Has(Feature::BodyTracking)) {
        Load("xrCreateBodyTrackerFB", m_createBodyTracker);
        Load("xrDestroyBodyTrackerFB", m_destroyBodyTracker);
        Load("xrLocateBodyJointsFB", m_locateBodyJoints);
    }
    if (Has(Feature::FaceTracking)) {
        Load("xrCreateFaceTracker2FB", m_createFaceTracker);
        Load("xrDestroyFaceTracker2FB", m_destroyFaceTracker);
        Load("xrGetFaceExpressionWeights2FB", m_faceWeights);
    }
    if (Has(Feature::PerformanceMetrics)) {
        Load("xrEnumeratePerformanceMetricsCounterPathsMETA", m_enumerateCounters);
        Load("xrSetPerformanceMetricsStateMETA", m_setMetricsState);
        Load("xrQueryPerformanceMetricsCounterMETA", m_queryCounter);
    }
#ifndef _WIN32
    if (m_hasTimespec) {
        Load("xrConvertTimeToTimespecTimeKHR", m_convertTime);
    }
#endif
}

void OpenXrRuntime::LoadDeviceFunctions(const device::vulkan::Native& native) {
    const auto getDevice = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        native.getInstanceProcAddr(native.instance, "vkGetDeviceProcAddr"));
    m_vkDevice = native.device;
    const auto load = [&](const char* name, auto& function) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            getDevice(native.device, name));
        if (function == nullptr) {
            throw Error("the Vulkan device has no {}", name);
        }
    };
    load("vkCreateCommandPool", m_vk.createCommandPool);
    load("vkDestroyCommandPool", m_vk.destroyCommandPool);
    load("vkAllocateCommandBuffers", m_vk.allocateCommandBuffers);
    load("vkResetCommandBuffer", m_vk.resetCommandBuffer);
    load("vkBeginCommandBuffer", m_vk.beginCommandBuffer);
    load("vkEndCommandBuffer", m_vk.endCommandBuffer);
    load("vkCmdPipelineBarrier", m_vk.cmdPipelineBarrier);
    load("vkCmdCopyBufferToImage", m_vk.cmdCopyBufferToImage);
}

double OpenXrRuntime::DisplayHostTime(XrTime display, double woke, double period) const {
#ifndef _WIN32
    timespec converted{};
    if (m_convertTime != nullptr && XR_SUCCEEDED(m_convertTime(m_instance, display, &converted))) {
        return double(converted.tv_sec) + double(converted.tv_nsec) * 1e-9;
    }
#endif
    (void)display;
    return woke + 2 * period;
}

void OpenXrRuntime::CheckGraphicsRequirements() {
    if (m_graphicsChecked) {
        return;
    }
    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    Check(m_graphicsRequirements(m_instance, m_system, &requirements),
          "xrGetVulkanGraphicsRequirements2KHR");
    if (requirements.minApiVersionSupported > XR_MAKE_VERSION(1, 3, 0)) {
        throw UnsupportedError(
            "the OpenXR runtime needs a Vulkan newer than Chrono Batch Render's 1.3");
    }
    m_graphicsChecked = true;
}

VkPhysicalDevice OpenXrRuntime::HeadsetAdapter(VkInstance instance) {
    XrVulkanGraphicsDeviceGetInfoKHR get{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    get.systemId = m_system;
    get.vulkanInstance = instance;
    VkPhysicalDevice adapter = VK_NULL_HANDLE;
    Check(m_graphicsDevice(m_instance, &get, &adapter), "xrGetVulkanGraphicsDevice2KHR");
    return adapter;
}

void OpenXrRuntime::ReadRefreshRates() {
    if (!Has(Feature::DisplayRefreshRate)) {
        return;
    }
    uint32_t count = 0;
    Check(m_enumerateRefreshRates(m_session, 0, &count, nullptr),
          "xrEnumerateDisplayRefreshRatesFB");
    m_rates.resize(count);
    Check(m_enumerateRefreshRates(m_session, count, &count, m_rates.data()),
          "xrEnumerateDisplayRefreshRatesFB");
    Check(m_getRefreshRate(m_session, &m_rate), "xrGetDisplayRefreshRateFB");
    m_hasRefreshRate = true;
    const float wanted = m_desc.refreshRate;
    if (wanted > 0 && wanted != m_rate) {
        if (std::ranges::find(m_rates, wanted) == m_rates.end()) {
            Log(LogLevel::Warning, "the headset has no {} Hz rate, so it keeps {} Hz", wanted,
                m_rate);
        } else {
            Check(m_requestRefreshRate(m_session, wanted), "xrRequestDisplayRefreshRateFB");
        }
    }
}

void OpenXrRuntime::StartMetrics() {
    if (!Has(Feature::PerformanceMetrics)) {
        return;
    }
    XrPerformanceMetricsStateMETA state{XR_TYPE_PERFORMANCE_METRICS_STATE_META};
    state.enabled = XR_TRUE;
    Check(m_setMetricsState(m_session, &state), "xrSetPerformanceMetricsStateMETA");
    uint32_t count = 0;
    Check(m_enumerateCounters(m_instance, 0, &count, nullptr),
          "xrEnumeratePerformanceMetricsCounterPathsMETA");
    std::vector<XrPath> paths(count);
    Check(m_enumerateCounters(m_instance, count, &count, paths.data()),
          "xrEnumeratePerformanceMetricsCounterPathsMETA");
    const std::pair<Counter, const char*> known[] = {
        {Counter::AppCpu, "/perfmetrics_meta/app/cpu_frametime"},
        {Counter::AppGpu, "/perfmetrics_meta/app/gpu_frametime"},
        {Counter::AppLatency, "/perfmetrics_meta/app/motion_to_photon_latency"},
        {Counter::CompositorCpu, "/perfmetrics_meta/compositor/cpu_frametime"},
        {Counter::CompositorGpu, "/perfmetrics_meta/compositor/gpu_frametime"},
        {Counter::Dropped, "/perfmetrics_meta/compositor/dropped_frame_count"},
        {Counter::CpuUse, "/perfmetrics_meta/device/cpu_utilization_average"},
        {Counter::GpuUse, "/perfmetrics_meta/device/gpu_utilization"},
    };
    for (const auto& [counter, name] : known) {
        const XrPath path = Path(name);
        if (std::ranges::find(paths, path) != paths.end()) {
            m_counters.emplace_back(counter, path);
        }
    }
}

void OpenXrRuntime::PollEvents() {
    CHRONO_BATCH_RENDER_ZONE("XR/Poll events");
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(m_instance, &event) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            ChangeState(reinterpret_cast<const XrEventDataSessionStateChanged&>(event).state);
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            m_recentered = true;
            break;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            ReadProfiles();
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            m_exit = true;
            break;
        case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB:
            m_rate = reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB&>(event)
                         .toDisplayRefreshRate;
            break;
        default:
            break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void OpenXrRuntime::ChangeState(XrSessionState state) {
    m_state = state;
    if (state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType = kStereo;
        Check(xrBeginSession(m_session, &begin), "xrBeginSession");
        m_running = true;
    } else if (state == XR_SESSION_STATE_STOPPING) {
        Check(xrEndSession(m_session), "xrEndSession");
        m_running = false;
    } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
        m_running = false;
        m_exit = true;
    }
}

bool OpenXrRuntime::RelocateViews(Frame& frame) {
    if (!m_frameBegun || !frame.shouldRender) {
        return false;
    }
    LocateViews(frame);
    return true;
}

void OpenXrRuntime::LocateViews(Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Locate views");
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = kStereo;
    locate.displayTime = m_displayTime;
    locate.space = m_space;
    XrViewState state{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    m_views[0] = {XR_TYPE_VIEW};
    m_views[1] = {XR_TYPE_VIEW};
    Check(xrLocateViews(m_session, &locate, &state, 2, &count, m_views), "xrLocateViews");
    constexpr XrViewStateFlags kValid =
        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    m_viewsValid = (state.viewStateFlags & kValid) == kValid;
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const XrFovf& fov = m_views[eye].fov;
        frame.views[eye].pose = StagePose(m_views[eye].pose, m_viewsValid);
        frame.views[eye].fov = {fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
    }
    frame.head = Locate(m_viewSpace);
}

Pose OpenXrRuntime::Locate(XrSpace space) const {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    Check(xrLocateSpace(space, m_space, m_displayTime, &location), "xrLocateSpace");
    return StagePose(location);
}

}

namespace chrono::batch_render::xr {

std::unique_ptr<Runtime> CreateOpenXrRuntime(const SessionDesc& desc) {
    return std::make_unique<openxr::OpenXrRuntime>(desc);
}

}
