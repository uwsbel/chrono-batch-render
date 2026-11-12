// SPDX-License-Identifier: BSD-3-Clause
#include <cmath>
#include <cstring>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/poses.h"
#include "chrono_batch_render/xr/rig/rig_state.h"

namespace chrono::batch_render::xr {
namespace {

double HeadingDegrees(Double3 forward) {
    return std::atan2(forward.y, forward.x) * 180 / std::numbers::pi;
}

Quatd Level(Quatd rotation) {
    return FromAxisAngle({0, 0, 1}, HeadingDegrees(Rotate(rotation, {1, 0, 0})));
}

Pose WorldPose(std::span<const double> words) {
    return {{words[0], words[1], words[2]}, {words[3], words[4], words[5], words[6]}, true};
}

batch_render::View FrameView(Scene& scene, const std::string& path) {
    return scene.Select(std::vector<std::string>{path});
}

Pose ReadWorldPose(Scene& scene, const std::string& path) {
    const std::vector<double> words =
        FrameView(scene, path).ReadWorldPoses(device::DType::Float64).ToHost<double>();
    return WorldPose(words);
}

bool Follows(Anchor mode) {
    return mode == Anchor::Prim || mode == Anchor::Follow || mode == Anchor::FollowSmoothed;
}

}

std::optional<std::string> RigState::FramePath() const {
    if (body) {
        return body->Path();
    }
    if (Follows(anchor.mode) && anchorTarget) {
        return anchorTarget->Path();
    }
    return parentPath;
}

bool RigState::FrameYaw() const {
    return !body && (anchor.mode == Anchor::Follow || anchor.mode == Anchor::FollowSmoothed);
}

void RigState::AdoptFrame(const Pose& world) {
    frameRotation = FrameYaw() ? Level(world.orientation) : world.orientation;
    framePosition = world.position;
}

void RigState::MirrorFrame() {
    CHRONO_BATCH_RENDER_ZONE("XR/Anchor frame");
    const std::optional<std::string> path = FramePath();
    if (!path) {
        frameRotation = {0, 0, 0, 1};
        framePosition = {0, 0, 0};
        readingFrame.clear();
        return;
    }
    if (!readingFrame.empty() && frameRead.Ready()) {
        if (readingFrame == *path) {
            const Span<const std::byte> bytes = frameRead.Bytes();
            if (bytes.size() >= 7 * sizeof(double)) {
                double words[7];
                std::memcpy(words, bytes.data(), sizeof(words));
                AdoptFrame(WorldPose(words));
            }
        }
        readingFrame.clear();
    }
    if (readingFrame.empty() && scene->Published() > 0) {
        frameRead = FrameView(*scene, *path)
                        .ReadWorldPoses(device::DType::Float64, Stream::None())
                        .ToHostAsync();
        readingFrame = *path;
    }
}

void RigState::ReadFrame() {
    readingFrame.clear();
    const std::optional<std::string> path = FramePath();
    if (!path) {
        frameRotation = {0, 0, 0, 1};
        framePosition = {0, 0, 0};
        return;
    }
    AdoptFrame(ReadWorldPose(*scene, *path));
}

void RigState::Follow(const std::optional<Prim>& target, bool yaw, bool scaled, float lag) {
    if (!target) {
        if (root.HasApi<FollowAPI>()) {
            scene->Unapply<FollowAPI>(Prim(root));
        }
        return;
    }
    if (!root.HasApi<FollowAPI>()) {
        scene->Apply<FollowAPI>(Prim(root));
    }
    root.Set(FollowAPI::kFollowTarget, std::vector<std::string>{target->Path()});
    root.Set(FollowAPI::kFollowRotate, yaw ? FollowRotate::Yaw : FollowRotate::Inherit);
    root.Set(FollowAPI::kFollowInheritScale, scaled);
    root.Set(FollowAPI::kFollowPositionLag, Float3{lag, lag, lag});
    root.Set(FollowAPI::kFollowRotationLag, lag);
}

void RigState::LandOnCamera(const Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Land on camera");
    cameraPending = false;
    const Pose camera = ReadWorldPose(*scene, anchorTarget->Path());
    const Double3 eye = camera.position;
    const Double3 looking = Rotate(camera.orientation, {0, 0, -1});
    const Double3 tracked = frame.head.valid ? frame.head.position : Double3{0, 0, 0};
    const double headHeading =
        frame.head.valid ? HeadingDegrees(Rotate(frame.head.orientation, {0, 0, -1})) : 90;
    const Quatd turn = FromAxisAngle({0, 0, 1}, HeadingDegrees(looking) - headHeading);
    const Double3 world = Add(eye, Scale(Rotate(turn, tracked), -userScale));
    const Quatd into = Conjugate(frameRotation);
    rootPose.orientation = Multiply(into, turn);
    rootPose.position = Rotate(into, Add(world, Scale(framePosition, -1)));
    if (anchor.floor) {
        rootPose.position.z = *anchor.floor;
    }
    WriteRoot();
}

void RigState::HoldHeight(const Frame& frame) {
    if (anchor.fixedHeight && frame.head.valid) {
        rootPose.position.z = *anchor.fixedHeight - userScale * frame.head.position.z;
        WriteRoot();
    }
}

std::optional<Prim> RigState::HitBody(const PointerHit& hit) const {
    if (hit.prim == kNoPrim || locomotion.bodies.empty()) {
        return std::nullopt;
    }
    for (std::optional<Prim> at = scene->ByIndex(hit.prim); at; at = at->Parent()) {
        for (const Prim& candidate : locomotion.bodies) {
            if (candidate.Index() == at->Index()) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

void RigState::Reframe(const Frame& frame, const PointerHit& hit, const std::optional<Prim>& onto) {
    const double heading = HeadingDegrees(Rotate(WorldRotation(), {1, 0, 0}));
    body = onto;
    ground.reset();
    if (onto) {
        Follow(onto, false, false, 0);
    } else {
        Follow(Follows(anchor.mode) ? anchorTarget : std::nullopt, anchor.mode != Anchor::Prim,
               anchor.mode == Anchor::Prim,
               anchor.mode == Anchor::FollowSmoothed ? anchor.smoothing : 0.0f);
    }
    ReadFrame();
    const Double3 tracked = frame.head.valid ? frame.head.position : Double3{0, 0, 0};
    rootPose.orientation =
        FromAxisAngle({0, 0, 1}, heading - HeadingDegrees(Rotate(frameRotation, {1, 0, 0})));
    const Double3 landing =
        Rotate(Conjugate(frameRotation), Add(hit.position, Scale(framePosition, -1)));
    rootPose.position =
        Add(landing, Scale(Rotate(rootPose.orientation, {tracked.x, tracked.y, 0}), -userScale));
    WriteRoot();
}

void Rig::SetAnchor(const AnchorDesc& desc) {
    RigState& state = *m_state;
    std::optional<Prim> target;
    if (desc.mode != Anchor::Origin) {
        if (desc.target.empty()) {
            throw Error("an anchor other than Origin needs a target");
        }
        target = state.scene->Find(desc.target);
        if (!target || !target->IsA<Xformable>()) {
            throw Error("there's no Xformable at {} to anchor to", desc.target);
        }
    }
    state.anchor = desc;
    state.anchorTarget = target;
    state.body.reset();
    state.ground.reset();
    state.Follow(Follows(desc.mode) ? target : std::nullopt, desc.mode != Anchor::Prim,
                 desc.mode == Anchor::Prim,
                 desc.mode == Anchor::FollowSmoothed ? desc.smoothing : 0.0f);
    state.rootPose = {{0, 0, 0}, {0, 0, 0, 1}, true};
    state.WriteRoot();
    state.ReadFrame();
    state.cameraPending = desc.mode == Anchor::Camera;
    if (state.lastFrame) {
        if (state.cameraPending) {
            state.LandOnCamera(*state.lastFrame);
        }
        state.HoldHeight(*state.lastFrame);
    }
}

const AnchorDesc& Rig::GetAnchor() const {
    return m_state->anchor;
}

std::optional<Prim> Rig::Body() const {
    return m_state->body;
}

}
