// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render::xr {

enum class Side : uint32_t { Left, Right };

enum class Feature : uint32_t {
    EyeGaze,
    HandTracking,
    DisplayRefreshRate,
    PerformanceMetrics,
    HandInteraction,
    BodyTracking,
    FaceTracking,
};

constexpr uint32_t kFeatureCount = 7;

struct Pose {
    Double3 position;
    Quatd orientation;
    bool valid = false;
};

struct Fov {
    float left = 0;
    float right = 0;
    float up = 0;
    float down = 0;
};

struct View {
    Pose pose;
    Fov fov;
};

struct Buttons {
    bool primary = false;
    bool secondary = false;
    bool thumbstick = false;
    bool menu = false;
};

struct Controller {
    bool active = false;
    Pose pose;
    Pose aim;
    Pose rawPose;
    Pose rawAim;
    float trigger = 0;
    float grip = 0;
    Float2 thumbstick;
    Buttons buttons;
};

enum class HandJoint : uint32_t {
    Palm,
    Wrist,
    ThumbMetacarpal,
    ThumbProximal,
    ThumbDistal,
    ThumbTip,
    IndexMetacarpal,
    IndexProximal,
    IndexIntermediate,
    IndexDistal,
    IndexTip,
    MiddleMetacarpal,
    MiddleProximal,
    MiddleIntermediate,
    MiddleDistal,
    MiddleTip,
    RingMetacarpal,
    RingProximal,
    RingIntermediate,
    RingDistal,
    RingTip,
    LittleMetacarpal,
    LittleProximal,
    LittleIntermediate,
    LittleDistal,
    LittleTip,
};

constexpr uint32_t kHandJointCount = 26;

struct JointPose {
    Pose pose;
    float radius = 0;
};

struct HandJoints {
    bool active = false;
    std::array<JointPose, kHandJointCount> joints;

    const JointPose& operator[](HandJoint joint) const {
        return joints[uint32_t(joint)];
    }

    JointPose& operator[](HandJoint joint) {
        return joints[uint32_t(joint)];
    }
};

enum class BodyJoint : uint32_t {
    Root,
    Hips,
    SpineLower,
    SpineMiddle,
    SpineUpper,
    Chest,
    Neck,
    Head,
    LeftShoulder,
    LeftScapula,
    LeftArmUpper,
    LeftArmLower,
    LeftHandWristTwist,
    RightShoulder,
    RightScapula,
    RightArmUpper,
    RightArmLower,
    RightHandWristTwist,
    LeftHandPalm,
    LeftHandWrist,
    LeftHandThumbMetacarpal,
    LeftHandThumbProximal,
    LeftHandThumbDistal,
    LeftHandThumbTip,
    LeftHandIndexMetacarpal,
    LeftHandIndexProximal,
    LeftHandIndexIntermediate,
    LeftHandIndexDistal,
    LeftHandIndexTip,
    LeftHandMiddleMetacarpal,
    LeftHandMiddleProximal,
    LeftHandMiddleIntermediate,
    LeftHandMiddleDistal,
    LeftHandMiddleTip,
    LeftHandRingMetacarpal,
    LeftHandRingProximal,
    LeftHandRingIntermediate,
    LeftHandRingDistal,
    LeftHandRingTip,
    LeftHandLittleMetacarpal,
    LeftHandLittleProximal,
    LeftHandLittleIntermediate,
    LeftHandLittleDistal,
    LeftHandLittleTip,
    RightHandPalm,
    RightHandWrist,
    RightHandThumbMetacarpal,
    RightHandThumbProximal,
    RightHandThumbDistal,
    RightHandThumbTip,
    RightHandIndexMetacarpal,
    RightHandIndexProximal,
    RightHandIndexIntermediate,
    RightHandIndexDistal,
    RightHandIndexTip,
    RightHandMiddleMetacarpal,
    RightHandMiddleProximal,
    RightHandMiddleIntermediate,
    RightHandMiddleDistal,
    RightHandMiddleTip,
    RightHandRingMetacarpal,
    RightHandRingProximal,
    RightHandRingIntermediate,
    RightHandRingDistal,
    RightHandRingTip,
    RightHandLittleMetacarpal,
    RightHandLittleProximal,
    RightHandLittleIntermediate,
    RightHandLittleDistal,
    RightHandLittleTip,
};

constexpr uint32_t kBodyJointCount = 70;

struct BodyJoints {
    bool active = false;
    float confidence = 0;
    std::array<Pose, kBodyJointCount> joints;

    const Pose& operator[](BodyJoint joint) const {
        return joints[uint32_t(joint)];
    }

    const Pose& Hand(Side side, HandJoint joint) const {
        const BodyJoint palm =
            side == Side::Left ? BodyJoint::LeftHandPalm : BodyJoint::RightHandPalm;
        return joints[uint32_t(palm) + uint32_t(joint)];
    }
};

enum class FaceExpression : uint32_t {
    BrowLowererL,
    BrowLowererR,
    CheekPuffL,
    CheekPuffR,
    CheekRaiserL,
    CheekRaiserR,
    CheekSuckL,
    CheekSuckR,
    ChinRaiserB,
    ChinRaiserT,
    DimplerL,
    DimplerR,
    EyesClosedL,
    EyesClosedR,
    EyesLookDownL,
    EyesLookDownR,
    EyesLookLeftL,
    EyesLookLeftR,
    EyesLookRightL,
    EyesLookRightR,
    EyesLookUpL,
    EyesLookUpR,
    InnerBrowRaiserL,
    InnerBrowRaiserR,
    JawDrop,
    JawSidewaysLeft,
    JawSidewaysRight,
    JawThrust,
    LidTightenerL,
    LidTightenerR,
    LipCornerDepressorL,
    LipCornerDepressorR,
    LipCornerPullerL,
    LipCornerPullerR,
    LipFunnelerLb,
    LipFunnelerLt,
    LipFunnelerRb,
    LipFunnelerRt,
    LipPressorL,
    LipPressorR,
    LipPuckerL,
    LipPuckerR,
    LipStretcherL,
    LipStretcherR,
    LipSuckLb,
    LipSuckLt,
    LipSuckRb,
    LipSuckRt,
    LipTightenerL,
    LipTightenerR,
    LipsToward,
    LowerLipDepressorL,
    LowerLipDepressorR,
    MouthLeft,
    MouthRight,
    NoseWrinklerL,
    NoseWrinklerR,
    OuterBrowRaiserL,
    OuterBrowRaiserR,
    UpperLidRaiserL,
    UpperLidRaiserR,
    UpperLipRaiserL,
    UpperLipRaiserR,
    TongueTipInterdental,
    TongueTipAlveolar,
    TongueFrontDorsalPalate,
    TongueMidDorsalPalate,
    TongueBackDorsalVelar,
    TongueOut,
    TongueRetreat,
};

constexpr uint32_t kFaceExpressionCount = 70;

enum class FaceRegion : uint32_t { Lower, Upper };

enum class FaceSource : uint32_t { Visual, Audio };

struct FaceExpressions {
    bool active = false;
    bool eyesFollow = false;
    FaceSource source = FaceSource::Visual;
    std::array<float, kFaceExpressionCount> weights{};
    std::array<float, 2> confidence{};

    float operator[](FaceExpression expression) const {
        return weights[uint32_t(expression)];
    }

    float Confidence(FaceRegion region) const {
        return confidence[uint32_t(region)];
    }
};

struct HapticPulse {
    Side hand = Side::Left;
    float amplitude = 0;
    double duration = 0;
    float frequency = 0;
};

enum class SessionState : uint32_t {
    Unknown,
    Idle,
    Ready,
    Synchronized,
    Visible,
    Focused,
    Stopping,
    LossPending,
    Exiting,
};

struct ActionState {
    std::string action;
    float value = 0;
    bool held = false;
    bool pressed = false;
    bool released = false;
    Side hand = Side::Right;
};

struct ActionEvent {
    std::string action;
    Side hand = Side::Right;
    bool pressed = false;
};

struct Frame {
    uint64_t index = 0;
    int64_t displayTime = 0;
    double deltaTime = 0;
    bool shouldRender = true;
    std::array<View, 2> views;
    Pose head;
    std::optional<Pose> gaze;
    std::array<xr::Controller, 2> controllers;
    std::array<HandJoints, 2> hands;
    BodyJoints body;
    FaceExpressions face;
    std::vector<HapticPulse> haptics;
    std::vector<ActionState> actions;
    std::vector<ActionEvent> events;
    std::array<std::string, 2> profiles;
    Side dominantHand = Side::Right;
    SessionState state = SessionState::Focused;
    bool recentered = false;
    uint32_t features = 0;

    const View& Eye(Side eye) const {
        return views[uint32_t(eye)];
    }

    const xr::Controller& Controller(Side hand) const {
        return controllers[uint32_t(hand)];
    }

    const HandJoints& Hand(Side hand) const {
        return hands[uint32_t(hand)];
    }

    void Haptics(Side hand, float amplitude, double duration = 0, float frequency = 0) {
        haptics.push_back({hand, amplitude, duration, frequency});
    }

    const ActionState* Action(std::string_view action) const {
        for (const ActionState& each : actions) {
            if (each.action == action) {
                return &each;
            }
        }
        return nullptr;
    }

    bool Pressed(std::string_view action) const {
        const ActionState* found = Action(action);
        return found != nullptr && found->pressed;
    }

    bool Released(std::string_view action) const {
        const ActionState* found = Action(action);
        return found != nullptr && found->released;
    }

    bool Held(std::string_view action) const {
        const ActionState* found = Action(action);
        return found != nullptr && found->held;
    }

    float Value(std::string_view action) const {
        const ActionState* found = Action(action);
        return found != nullptr ? found->value : 0.0f;
    }

    bool Has(Feature feature) const {
        return (features >> uint32_t(feature) & 1u) != 0;
    }
};

}
