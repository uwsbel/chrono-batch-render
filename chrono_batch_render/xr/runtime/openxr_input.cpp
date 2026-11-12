// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/runtime/openxr.h"

namespace chrono::batch_render::xr::openxr {

XrPath OpenXrRuntime::Path(const std::string& text) const {
    XrPath path = XR_NULL_PATH;
    Check(xrStringToPath(m_instance, text.c_str(), &path), "xrStringToPath");
    return path;
}

XrAction OpenXrRuntime::CreateAction(const char* name, const char* label, XrActionType type,
                                     bool hands) {
    XrActionCreateInfo create{XR_TYPE_ACTION_CREATE_INFO};
    CopyName(create.actionName, XR_MAX_ACTION_NAME_SIZE, name);
    CopyName(create.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, label);
    create.actionType = type;
    XrPath paths[2] = {m_hands[0].path, m_hands[1].path};
    if (hands) {
        create.countSubactionPaths = 2;
        create.subactionPaths = paths;
    }
    XrAction action = XR_NULL_HANDLE;
    Check(xrCreateAction(m_actionSet, &create, &action), "xrCreateAction");
    return action;
}

void OpenXrRuntime::CreateActions() {
    XrActionSetCreateInfo create{XR_TYPE_ACTION_SET_CREATE_INFO};
    CopyName(create.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "chrono_batch_render");
    CopyName(create.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
             "Chrono Batch Render");
    Check(xrCreateActionSet(m_instance, &create, &m_actionSet), "xrCreateActionSet");
    m_hands[0].path = Path("/user/hand/left");
    m_hands[1].path = Path("/user/hand/right");
    m_actions[uint32_t(Slot::GripPose)] =
        CreateAction("grip_pose", "Grip pose", XR_ACTION_TYPE_POSE_INPUT, true);
    m_actions[uint32_t(Slot::AimPose)] =
        CreateAction("aim_pose", "Aim pose", XR_ACTION_TYPE_POSE_INPUT, true);
    m_actions[uint32_t(Slot::Trigger)] =
        CreateAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true);
    m_actions[uint32_t(Slot::Squeeze)] =
        CreateAction("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true);
    m_actions[uint32_t(Slot::Stick)] =
        CreateAction("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true);
    m_actions[uint32_t(Slot::StickClick)] =
        CreateAction("thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
    m_actions[uint32_t(Slot::Primary)] =
        CreateAction("primary", "Primary button", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
    m_actions[uint32_t(Slot::Secondary)] =
        CreateAction("secondary", "Secondary button", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
    m_actions[uint32_t(Slot::Menu)] =
        CreateAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
    m_actions[uint32_t(Slot::Haptic)] =
        CreateAction("haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, true);
    if (Has(Feature::EyeGaze)) {
        m_gaze = CreateAction("gaze", "Eye gaze", XR_ACTION_TYPE_POSE_INPUT, false);
    }
    for (const InteractionProfile& profile : InteractionProfiles()) {
        if ((!profile.extension.empty() &&
             std::ranges::find(m_enabled, profile.extension) == m_enabled.end()) ||
            (profile.feature && !Has(*profile.feature))) {
            continue;
        }
        std::vector<std::pair<XrAction, std::string>> bindings;
        for (const ComponentBinding& binding : profile.bindings) {
            bindings.emplace_back(m_actions[uint32_t(binding.slot)], BindingPath(binding));
        }
        Suggest(profile.path, bindings);
    }
    if (Has(Feature::EyeGaze)) {
        Suggest("/interaction_profiles/ext/eye_gaze_interaction",
                {{m_gaze, "/user/eyes_ext/input/gaze_ext/pose"}});
    }
}

void OpenXrRuntime::Suggest(const std::string& profile,
                            const std::vector<std::pair<XrAction, std::string>>& bindings) {
    std::vector<XrActionSuggestedBinding> suggested;
    for (const auto& [action, path] : bindings) {
        suggested.push_back({action, Path(path)});
    }
    XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestion.interactionProfile = Path(profile);
    suggestion.countSuggestedBindings = uint32_t(suggested.size());
    suggestion.suggestedBindings = suggested.data();
    const XrResult result = xrSuggestInteractionProfileBindings(m_instance, &suggestion);
    if (XR_FAILED(result)) {
        Log(LogLevel::Warning, "the OpenXR runtime refused bindings for {} (XrResult {})", profile,
            int(result));
    }
}

XrSpace OpenXrRuntime::CreateReferenceSpace(XrReferenceSpaceType type) {
    XrReferenceSpaceCreateInfo create{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    create.referenceSpaceType = type;
    create.poseInReferenceSpace.orientation.w = 1;
    XrSpace space = XR_NULL_HANDLE;
    Check(xrCreateReferenceSpace(m_session, &create, &space), "xrCreateReferenceSpace");
    return space;
}

XrSpace OpenXrRuntime::CreateActionSpace(XrAction action, XrPath hand) {
    XrActionSpaceCreateInfo create{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    create.action = action;
    create.subactionPath = hand;
    create.poseInActionSpace.orientation.w = 1;
    XrSpace space = XR_NULL_HANDLE;
    Check(xrCreateActionSpace(m_session, &create, &space), "xrCreateActionSpace");
    return space;
}

void OpenXrRuntime::CreateSpaces() {
    uint32_t count = 0;
    Check(xrEnumerateReferenceSpaces(m_session, 0, &count, nullptr), "xrEnumerateReferenceSpaces");
    std::vector<XrReferenceSpaceType> types(count);
    Check(xrEnumerateReferenceSpaces(m_session, count, &count, types.data()),
          "xrEnumerateReferenceSpaces");
    const bool wantsStage = m_desc.referenceSpace == ReferenceSpace::Stage;
    const bool stage =
        wantsStage && std::ranges::find(types, XR_REFERENCE_SPACE_TYPE_STAGE) != types.end();
    if (wantsStage && !stage) {
        Log(LogLevel::Warning, "the headset has no stage space, so poses are in its local one");
    }
    m_space =
        CreateReferenceSpace(stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL);
    m_viewSpace = CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_VIEW);
    for (HandSpaces& hand : m_hands) {
        hand.grip = CreateActionSpace(m_actions[uint32_t(Slot::GripPose)], hand.path);
        hand.aim = CreateActionSpace(m_actions[uint32_t(Slot::AimPose)], hand.path);
    }
    if (Has(Feature::EyeGaze)) {
        m_gazeSpace = CreateActionSpace(m_gaze, XR_NULL_PATH);
    }
}

void OpenXrRuntime::ReadProfiles() {
    if (m_session == XR_NULL_HANDLE) {
        return;
    }
    for (uint32_t side = 0; side < 2; ++side) {
        XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
        m_profiles[side].clear();
        if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(m_session, m_hands[side].path, &state)) &&
            state.interactionProfile != XR_NULL_PATH) {
            char text[XR_MAX_PATH_LENGTH];
            uint32_t length = 0;
            if (XR_SUCCEEDED(xrPathToString(m_instance, state.interactionProfile,
                                            XR_MAX_PATH_LENGTH, &length, text))) {
                m_profiles[side] = text;
            }
        }
    }
}

XrActionStateGetInfo OpenXrRuntime::StateInfo(Slot slot, XrPath hand) const {
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = m_actions[uint32_t(slot)];
    get.subactionPath = hand;
    return get;
}

float OpenXrRuntime::ReadFloat(Slot slot, XrPath hand) const {
    const XrActionStateGetInfo get = StateInfo(slot, hand);
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    Check(xrGetActionStateFloat(m_session, &get, &state), "xrGetActionStateFloat");
    return state.isActive == XR_TRUE ? state.currentState : 0.0f;
}

bool OpenXrRuntime::ReadBool(Slot slot, XrPath hand) const {
    const XrActionStateGetInfo get = StateInfo(slot, hand);
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    Check(xrGetActionStateBoolean(m_session, &get, &state), "xrGetActionStateBoolean");
    return state.isActive == XR_TRUE && state.currentState == XR_TRUE;
}

bool OpenXrRuntime::PoseActive(XrAction action, XrPath hand) const {
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = action;
    get.subactionPath = hand;
    XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
    Check(xrGetActionStatePose(m_session, &get, &state), "xrGetActionStatePose");
    return state.isActive == XR_TRUE;
}

void OpenXrRuntime::ReadInputs(Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Actions");
    XrActiveActionSet active{m_actionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    Check(xrSyncActions(m_session, &sync), "xrSyncActions");
    for (uint32_t side = 0; side < 2; ++side) {
        const HandSpaces& hand = m_hands[side];
        Controller& controller = frame.controllers[side];
        controller.active = PoseActive(m_actions[uint32_t(Slot::GripPose)], hand.path);
        if (controller.active) {
            controller.pose = Locate(hand.grip);
            controller.aim = Locate(hand.aim);
        }
        controller.trigger = ReadFloat(Slot::Trigger, hand.path);
        controller.grip = ReadFloat(Slot::Squeeze, hand.path);
        const XrActionStateGetInfo get = StateInfo(Slot::Stick, hand.path);
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        Check(xrGetActionStateVector2f(m_session, &get, &stick), "xrGetActionStateVector2f");
        if (stick.isActive == XR_TRUE) {
            controller.thumbstick = {stick.currentState.x, stick.currentState.y};
        }
        controller.buttons.primary = ReadBool(Slot::Primary, hand.path);
        controller.buttons.secondary = ReadBool(Slot::Secondary, hand.path);
        controller.buttons.thumbstick = ReadBool(Slot::StickClick, hand.path);
        controller.buttons.menu = ReadBool(Slot::Menu, hand.path);
    }
    LocateHands(frame);
    LocateBody(frame);
    ReadFace(frame);
    frame.profiles = {m_profiles[0], m_profiles[1]};
    frame.recentered = std::exchange(m_recentered, false);
    if (Has(Feature::EyeGaze) && PoseActive(m_gaze, XR_NULL_PATH)) {
        const Pose gaze = Locate(m_gazeSpace);
        if (gaze.valid) {
            frame.gaze = gaze;
        }
    }
}

void OpenXrRuntime::SendHaptics(const Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Haptics");
    for (const HapticPulse& pulse : frame.haptics) {
        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
        vibration.duration =
            pulse.duration > 0 ? XrDuration(pulse.duration * 1e9) : XR_MIN_HAPTIC_DURATION;
        vibration.frequency = pulse.frequency > 0 ? pulse.frequency : XR_FREQUENCY_UNSPECIFIED;
        vibration.amplitude = std::clamp(pulse.amplitude, 0.0f, 1.0f);
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        info.action = m_actions[uint32_t(Slot::Haptic)];
        info.subactionPath = m_hands[uint32_t(pulse.hand)].path;
        Check(xrApplyHapticFeedback(m_session, &info,
                                    reinterpret_cast<XrHapticBaseHeader*>(&vibration)),
              "xrApplyHapticFeedback");
    }
}

}
