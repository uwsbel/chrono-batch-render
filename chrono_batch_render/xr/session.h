// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/device/tensor.h"
#include "chrono_batch_render/stage/schema/types.h"
#include "chrono_batch_render/xr/frame.h"
#include "chrono_batch_render/xr_export.h"

namespace chrono::batch_render {
class RenderOutput;
}

namespace chrono::batch_render::xr {

enum class Backend : uint32_t { OpenXR, Simulated };

enum class BlendMode : uint32_t { Opaque, Additive, AlphaBlend };

enum class ReferenceSpace : uint32_t { Stage, Local };

enum class Input : uint32_t {
    Trigger,
    Grip,
    Primary,
    Secondary,
    ThumbstickClick,
    Menu,
    ThumbstickUp,
    ThumbstickDown,
    ThumbstickLeft,
    ThumbstickRight,
};

enum class HandRole : uint32_t { Left, Right, Dominant, Offhand, Either };

struct ActionBinding {
    std::string action;
    Input input = Input::Trigger;
    HandRole hand = HandRole::Dominant;
    float threshold = 0.5f;
    std::string profile;
};

struct PoseFilter {
    Pose offset{{0, 0, 0}, {0, 0, 0, 1}, true};
    float smoothing = 0;
};

struct SimulatedProfile {
    std::string name;
    std::string interactionProfile = "/interaction_profiles/oculus/touch_controller";
    Uint2 resolution;
    std::array<Fov, 2> fov;
    float eyeSeparation = 0;
    float eyeHeight = 0;
    std::vector<float> refreshRates;

    CHRONO_BATCH_RENDER_XR_API static SimulatedProfile Quest3();
};

enum class HandPose : uint32_t { Open, Pinch, Point, Fist };

struct SimulatedInput {
    Pose head;
    std::optional<Pose> gaze;
    std::array<Controller, 2> controllers;
    std::array<HandJoints, 2> hands;
    BodyJoints body;
    FaceExpressions face;
    bool visible = true;
    std::array<std::map<std::string, float, std::less<>>, 2> components;
    std::string profile;
    bool loseSession = false;
    bool recenter = false;
    uint32_t skipRefreshes = 0;
    double lateBy = 0;

    CHRONO_BATCH_RENDER_XR_API void Press(Side hand, std::string_view component, float value = 1);
    CHRONO_BATCH_RENDER_XR_API void PoseHand(Side hand, HandPose pose);
};

struct SimulatedOffers {
    bool stageBounds = true;
    bool refreshRates = true;
    bool depthLayer = true;
    bool additive = true;
    bool alphaBlend = true;
    bool performanceMetrics = true;
    bool handTracking = true;
    bool eyeGaze = true;
    bool handInteraction = true;
    bool bodyTracking = true;
    bool faceTracking = true;
};

struct SimulatedDesc {
    SimulatedProfile profile = SimulatedProfile::Quest3();
    bool realTime = false;
    std::function<void(uint64_t frame, double time, SimulatedInput& input)> script;
    Float2 playArea{3, 3};
    SimulatedOffers offers;
    bool skipStartup = true;
    float userHeight = 0;
    float ipd = 0;
    bool placeControllers = false;
};

struct SessionDesc {
    std::string application = "Chrono Batch Render";
    Backend backend = Backend::OpenXR;
    ReferenceSpace referenceSpace = ReferenceSpace::Stage;
    bool depthLayer = true;
    float nearPlane = 0.05f;
    float farPlane = 1000;
    BlendMode blendMode = BlendMode::Opaque;
    float refreshRate = 0;
    float resolutionScale = 1;
    std::vector<Feature> features;
    Side dominantHand = Side::Right;
    SimulatedDesc simulated;
};

struct SessionStatus {
    Backend backend = Backend::Simulated;
    std::string runtime;
    std::string system;
    std::string state;
    Uint2 resolution;
    float refreshRate = 0;
    std::vector<float> refreshRates;
    uint32_t features = 0;
    uint64_t framesBegun = 0;
    uint64_t framesPresented = 0;
    Float2 playArea;
    bool depthLayer = false;
    BlendMode blendMode = BlendMode::Opaque;
};

struct FrameTiming {
    uint64_t index = 0;
    int64_t displayTime = 0;
    double wait = 0;
    double cpu = 0;
    double gpu = 0;
    double poseToSubmit = 0;
    double motionToPhoton = 0;
    double lateness = 0;
    uint32_t missed = 0;
    bool rendered = false;
    bool late = false;
    bool gpuDone = false;
};

struct TimingSummary {
    double mean = 0;
    double p95 = 0;
    double max = 0;
};

struct RuntimeMetrics {
    bool available = false;
    double appCpu = 0;
    double appGpu = 0;
    double compositorCpu = 0;
    double compositorGpu = 0;
    double motionToPhoton = 0;
    uint32_t droppedFrames = 0;
    float cpuUtilization = 0;
    float gpuUtilization = 0;
};

struct SessionStats {
    double period = 0;
    bool estimated = false;
    uint64_t frames = 0;
    uint64_t presented = 0;
    uint64_t notRendered = 0;
    uint64_t late = 0;
    uint64_t missed = 0;
    TimingSummary wait, cpu, gpu, poseToSubmit, motionToPhoton;
    std::vector<FrameTiming> recent;
    RuntimeMetrics runtimeMetrics;
};

class Rig;
class SessionImpl;

class CHRONO_BATCH_RENDER_XR_API Session {
  public:
    explicit Session(const SessionDesc& desc = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Ref<device::Device> CreateDevice(device::DeviceDesc desc = {});
    Ref<device::Device> GetDevice() const;

    bool Running();
    Frame BeginFrame();
    void EndFrame(const Frame& frame, const RenderOutput& output);
    void RequestExit();
    void LatchViews(Frame& frame);
    bool SetRefreshRate(float hz);

    void PushBindings(std::string_view name, const std::vector<ActionBinding>& bindings);
    void PopBindings(std::string_view name);
    std::vector<std::string> BindingLayers() const;

    void SetPoseFilter(Side hand, const PoseFilter& filter);
    PoseFilter GetPoseFilter(Side hand) const;

    device::Tensor Presented() const;
    device::Tensor PresentedDepth() const;
    SessionStatus Status() const;
    SessionStats Stats() const;
    void WriteTimings(const std::filesystem::path& path) const;
    const SessionDesc& GetDesc() const;

  private:
    friend class Rig;

    std::unique_ptr<SessionImpl> m_impl;
};

}
