// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/overlay/overlay.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/stage/schema/ids.h"
#include "chrono_batch_render/stage/schema/types.h"
#include "chrono_batch_render/xr/frame.h"
#include "chrono_batch_render/xr_export.h"

namespace chrono::batch_render {
class RayCaster;
struct RayHits;
}

namespace chrono::batch_render::xr {

struct RigDesc {
    std::string path = "/XR";
    std::vector<std::string> extraVars;
    std::vector<std::string> overlays = {"*"};
};

struct RigVisuals {
    bool controllers = true;
    bool hands = true;
    bool lasers = true;
    bool teleport = false;
    bool playArea = true;
    Float4 color = {0.3f, 0.75f, 1, 0.9f};
    Float4 invalidColor = {0.95f, 0.3f, 0.25f, 0.9f};
    std::string layer = "xr";
};

struct PointerDesc {
    float maxDistance = 30;
    float arcSpeed = 7;
    float gravity = 9.81f;
    uint32_t arcSegments = 24;
    float arcTime = 2;
    float maxSlopeDegrees = 35;
    bool wait = true;
    std::vector<Prim> ignore;
};

struct PointerHit {
    bool active = false;
    bool hit = false;
    bool valid = false;
    Double3 position;
    Double3 normal;
    Double3 localPosition;
    double distance = 0;
    uint32_t prim = kNoPrim;
    std::vector<Double3> path;
    uint64_t frame = 0;
};

enum class MoveMode : uint32_t { Smooth, Grounded, Fly };

struct LocomotionDesc {
    double snapDegrees = 30;
    double backStep = 1;
    MoveMode moveMode = MoveMode::Smooth;
    double speed = 1.5;
    double flySpeed = 3;
    bool floorAligned = true;
    std::vector<Prim> bodies;
};

enum class Anchor : uint32_t { Origin, Camera, Prim, Follow, FollowSmoothed };

struct AnchorDesc {
    Anchor mode = Anchor::Origin;
    std::string target;
    float smoothing = 0.3f;
    std::optional<double> fixedHeight;
    std::optional<double> floor;
};

class Session;
struct RigState;
class Rig;

class CHRONO_BATCH_RENDER_XR_API StandardControls {
  public:
    void Apply(Rig& rig, const Frame& frame);

    bool Aiming() const {
        return m_aiming;
    }

  private:
    bool m_aiming = false;
    bool m_turned = false;
};

enum class PointerGesture : uint32_t { Hover, Leave, Press, Release, Click };

struct CHRONO_BATCH_RENDER_XR_API PointerTarget {
    uint32_t prim = kNoPrim;
    std::optional<OverlayHit> overlay;
    Double3 position;
    double distance = 0;

    bool Same(const PointerTarget& other) const;
};

struct PointerEvent {
    PointerGesture gesture = PointerGesture::Hover;
    Side hand = Side::Right;
    PointerTarget target;
    Double3 origin;
    Double3 direction;
};

class CHRONO_BATCH_RENDER_XR_API Pointing {
  public:
    std::vector<PointerEvent> Update(Rig& rig, Overlays& overlays, const Frame& frame);
    void Draw(Overlays& overlays, const RigVisuals& visuals = {}) const;

    const std::optional<PointerTarget>& Target() const {
        return m_target;
    }

    bool Aimed() const {
        return m_aimed;
    }

    Double3 Origin() const {
        return m_origin;
    }

    Double3 Direction() const {
        return m_direction;
    }

  private:
    struct Sample {
        OverlayPick pick;
        Double3 origin;
        Double3 direction;
        double reach = 0;
    };

    std::vector<Sample> m_samples;
    std::optional<PointerTarget> m_target;
    std::optional<PointerTarget> m_pressed;
    bool m_down = false;
    bool m_aimed = false;
    Double3 m_origin;
    Double3 m_direction;
};

struct PointerHaptics {
    float hover = 0.15f;
    float press = 0.5f;
    double duration = 0.015;
};

CHRONO_BATCH_RENDER_XR_API void Buzz(Frame& frame, const std::vector<PointerEvent>& events,
                                     const PointerHaptics& feel = {});

struct GrabSnapping {
    double grid = 0;
    double turnDegrees = 0;
    double surface = 0;
};

class CHRONO_BATCH_RENDER_XR_API Grabbing {
  public:
    struct Moved {
        std::string path;
        Double3 translateBefore;
        Quatd orientBefore;
        Float3 scaleBefore;
        Double3 translate;
        Quatd orient;
        Float3 scale;
    };

    std::optional<Moved> Update(Rig& rig, const Frame& frame, const Pointing& pointing);

    const std::optional<std::string>& Held() const {
        return m_held;
    }

    bool TwoHanded() const {
        return m_twoHanded;
    }

    void SetSnapping(const GrabSnapping& snapping) {
        m_snapping = snapping;
    }

    const GrabSnapping& GetSnapping() const {
        return m_snapping;
    }

  private:
    void Hold(const Pose& hand);
    void Snap(const Ref<Scene>& scene, bool last);

    std::optional<std::string> m_held;
    bool m_down = false;
    bool m_otherDown = false;
    bool m_twoHanded = false;
    Double3 m_offset;
    Quatd m_turn;
    Double3 m_position;
    Quatd m_rotation;
    Double3 m_startMiddle;
    Double3 m_startSpan;
    Double3 m_startPosition;
    Quatd m_startRotation;
    Float3 m_startScale;
    Double3 m_parentOrigin;
    Quatd m_parentTurn;
    Double3 m_parentScale{1, 1, 1};
    Moved m_move;
    GrabSnapping m_snapping;
    double m_footing = 0;
    std::vector<Prim> m_carried;
    std::shared_ptr<RayCaster> m_caster;
    std::shared_ptr<RayHits> m_drop;
    double m_dropFrom = 0;
    std::optional<double> m_ground;
};

class CHRONO_BATCH_RENDER_XR_API Rig {
  public:
    Rig(Ref<Scene> scene, Session& session, const RigDesc& desc = {});

    XformPrim Root() const;
    XformPrim Head() const;
    CameraPrim Eye(Side eye) const;
    XformPrim Hand(Side hand) const;
    const std::string& Product() const;
    const std::string& ColorVar() const;
    const std::vector<std::string>& Vars() const;
    Uint2 RecommendedResolution() const;

    void Update(const Frame& frame);
    void UpdateViews(const Frame& frame);
    void SetRootPose(const Pose& pose);
    Pose RootPose() const;
    Pose WorldPose(const Pose& pose) const;

    void SetPointers(const PointerDesc& desc);
    const PointerDesc& GetPointers() const;
    PointerHit Laser(Side hand);
    PointerHit Teleport(Side hand);

    void SetLocomotion(const LocomotionDesc& desc);
    const LocomotionDesc& GetLocomotion() const;
    bool TeleportTo(const Frame& frame, const PointerHit& hit);
    void StepBack(const Frame& frame);
    void SnapTurn(const Frame& frame, int direction);
    void Move(const Frame& frame, Float2 stick, float rise = 0);
    void SetUserScale(double scale);
    double UserScale() const;

    void Draw(Overlays& overlays, const Frame& frame, const RigVisuals& visuals = {});

    const Ref<Scene>& GetScene() const;

    void SetAnchor(const AnchorDesc& desc);
    const AnchorDesc& GetAnchor() const;
    std::optional<Prim> Body() const;

  private:
    std::shared_ptr<RigState> m_state;
};

}
