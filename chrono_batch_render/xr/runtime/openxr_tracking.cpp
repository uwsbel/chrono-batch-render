// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/runtime/openxr.h"

namespace chrono::batch_render::xr::openxr {

static_assert(XR_HAND_JOINT_COUNT_EXT == kHandJointCount, "HandJoint follows XrHandJointEXT");
static_assert(XR_BODY_JOINT_COUNT_FB == kBodyJointCount, "BodyJoint follows XrBodyJointFB");
static_assert(uint32_t(BodyJoint::LeftHandPalm) == XR_BODY_JOINT_LEFT_HAND_PALM_FB &&
                  uint32_t(BodyJoint::RightHandLittleTip) == XR_BODY_JOINT_RIGHT_HAND_LITTLE_TIP_FB,
              "BodyJoint follows XrBodyJointFB");
static_assert(XR_FACE_EXPRESSION2_COUNT_FB == kFaceExpressionCount,
              "FaceExpression follows XrFaceExpression2FB");
static_assert(uint32_t(FaceExpression::TongueRetreat) == XR_FACE_EXPRESSION2_TONGUE_RETREAT_FB,
              "FaceExpression follows XrFaceExpression2FB");

void OpenXrRuntime::CreateHandTrackers() {
    if (!Has(Feature::HandTracking)) {
        return;
    }
    for (uint32_t side = 0; side < 2; ++side) {
        XrHandTrackerCreateInfoEXT create{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        create.hand = side == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        create.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        Check(m_createHandTracker(m_session, &create, &m_handTrackers[side]),
              "xrCreateHandTrackerEXT");
    }
}

void OpenXrRuntime::LocateHands(Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Hand tracking");
    if (!Has(Feature::HandTracking)) {
        return;
    }
    for (uint32_t side = 0; side < 2; ++side) {
        XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
        XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locations.jointLocations = joints;
        XrHandJointsLocateInfoEXT locate{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        locate.baseSpace = m_space;
        locate.time = m_displayTime;
        Check(m_locateHandJoints(m_handTrackers[side], &locate, &locations),
              "xrLocateHandJointsEXT");
        HandJoints& hand = frame.hands[side];
        hand.active = locations.isActive == XR_TRUE;
        for (uint32_t joint = 0; joint < kHandJointCount; ++joint) {
            const XrHandJointLocationEXT& location = joints[joint];
            hand.joints[joint].pose = StagePose(
                location.pose, hand.active && (location.locationFlags & kLocated) == kLocated);
            hand.joints[joint].radius = location.radius;
        }
    }
}

void OpenXrRuntime::CreateBodyAndFaceTrackers() {
    if (Has(Feature::BodyTracking)) {
        XrBodyTrackerCreateInfoFB create{XR_TYPE_BODY_TRACKER_CREATE_INFO_FB};
        create.bodyJointSet = XR_BODY_JOINT_SET_DEFAULT_FB;
        Check(m_createBodyTracker(m_session, &create, &m_bodyTracker), "xrCreateBodyTrackerFB");
    }
    if (Has(Feature::FaceTracking)) {
        XrFaceTrackerCreateInfo2FB create{XR_TYPE_FACE_TRACKER_CREATE_INFO2_FB};
        create.faceExpressionSet = XR_FACE_EXPRESSION_SET2_DEFAULT_FB;
        create.requestedDataSourceCount = uint32_t(m_faceSources.size());
        create.requestedDataSources = m_faceSources.data();
        Check(m_createFaceTracker(m_session, &create, &m_faceTracker), "xrCreateFaceTracker2FB");
    }
}

void OpenXrRuntime::LocateBody(Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Body tracking");
    if (m_bodyTracker == XR_NULL_HANDLE) {
        return;
    }
    XrBodyJointLocationFB joints[XR_BODY_JOINT_COUNT_FB];
    XrBodyJointLocationsFB locations{XR_TYPE_BODY_JOINT_LOCATIONS_FB};
    locations.jointCount = XR_BODY_JOINT_COUNT_FB;
    locations.jointLocations = joints;
    XrBodyJointsLocateInfoFB locate{XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB};
    locate.baseSpace = m_space;
    locate.time = m_displayTime;
    Check(m_locateBodyJoints(m_bodyTracker, &locate, &locations), "xrLocateBodyJointsFB");
    BodyJoints& body = frame.body;
    body.active = locations.isActive == XR_TRUE;
    body.confidence = locations.confidence;
    for (uint32_t joint = 0; joint < kBodyJointCount; ++joint) {
        body.joints[joint] =
            StagePose(joints[joint].pose,
                      body.active && (joints[joint].locationFlags & kLocated) == kLocated);
    }
}

void OpenXrRuntime::ReadFace(Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Face tracking");
    if (m_faceTracker == XR_NULL_HANDLE) {
        return;
    }
    FaceExpressions& face = frame.face;
    XrFaceExpressionWeights2FB weights{XR_TYPE_FACE_EXPRESSION_WEIGHTS2_FB};
    weights.weightCount = XR_FACE_EXPRESSION2_COUNT_FB;
    weights.weights = face.weights.data();
    weights.confidenceCount = XR_FACE_CONFIDENCE2_COUNT_FB;
    weights.confidences = face.confidence.data();
    XrFaceExpressionInfo2FB info{XR_TYPE_FACE_EXPRESSION_INFO2_FB};
    info.time = m_displayTime;
    Check(m_faceWeights(m_faceTracker, &info, &weights), "xrGetFaceExpressionWeights2FB");
    face.active = weights.isValid == XR_TRUE;
    face.eyesFollow = weights.isEyeFollowingBlendshapesValid == XR_TRUE;
    face.source = weights.dataSource == XR_FACE_TRACKING_DATA_SOURCE2_AUDIO_FB ? FaceSource::Audio
                                                                               : FaceSource::Visual;
    if (!face.active) {
        face.weights.fill(0);
        face.confidence.fill(0);
    }
}

}
