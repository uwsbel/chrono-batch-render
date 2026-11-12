// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dlpack/dlpack.h>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/render/ray_cast/ray_cast.h"
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/poses.h"
#include "chrono_batch_render/xr/rig.h"

namespace chrono::batch_render::xr {
namespace {

constexpr float kSqueezed = 0.6f;
constexpr float kLetGo = 0.4f;

double Length(Double3 v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Double3 Unit(Double3 v) {
    const double length = Length(v);
    return length > 1e-12 ? Scale(v, 1 / length) : v;
}

struct Placed {
    Double3 origin;
    Quatd turn;
    Double3 scale;
};

std::vector<Placed> ReadPlaced(Scene& scene, const std::vector<std::string>& paths) {
    const batch_render::View view = scene.Select(paths);
    const std::vector<double> poses = view.ReadWorldPoses(device::DType::Float64).ToHost<double>();
    const std::vector<double> scales =
        view.ReadWorldScales(device::DType::Float64).ToHost<double>();
    std::vector<Placed> placed;
    for (size_t at = 0; at < paths.size(); ++at) {
        const double* pose = poses.data() + at * 7;
        const double* scale = scales.data() + at * 3;
        placed.push_back({{pose[0], pose[1], pose[2]},
                          Normalize(Quatd{pose[3], pose[4], pose[5], pose[6]}),
                          {scale[0], scale[1], scale[2]}});
    }
    return placed;
}

Quatd Between(Double3 a, Double3 b) {
    a = Unit(a);
    b = Unit(b);
    const Double3 axis{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    const double dot = a.x * b.x + a.y * b.y + a.z * b.z;
    if (dot < -0.999999) {
        const Double3 side = std::abs(a.x) < 0.9 ? Double3{1, 0, 0} : Double3{0, 1, 0};
        const Double3 normal = Unit({a.y * side.z - a.z * side.y, a.z * side.x - a.x * side.z,
                                     a.x * side.y - a.y * side.x});
        return {normal.x, normal.y, normal.z, 0};
    }
    return Normalize(Quatd{axis.x, axis.y, axis.z, 1 + dot});
}

std::optional<Prim> Movable(const Scene& scene, uint32_t index) {
    for (std::optional<Prim> at = scene.ByIndex(index); at; at = at->Parent()) {
        if (at->IsSession()) {
            return std::nullopt;
        }
        if (at->IsA<Xformable>()) {
            if (at->HasApi<FollowAPI>()) {
                return std::nullopt;
            }
            return at;
        }
    }
    return std::nullopt;
}

std::optional<Prim> ParentFrame(const Prim& prim) {
    if (prim.Get(Xformable::kResetXformStack)) {
        return std::nullopt;
    }
    for (std::optional<Prim> up = prim.Parent(); up; up = up->Parent()) {
        if (up->IsA<Xformable>()) {
            return up;
        }
    }
    return std::nullopt;
}

void Gather(const Prim& prim, std::vector<Prim>& prims) {
    prims.push_back(prim);
    for (const Prim& child : prim.Children()) {
        Gather(child, prims);
    }
}

device::Tensor Upload(device::Device& device, const float (&vector)[3]) {
    int64_t shape[2] = {1, 3};
    DLTensor tensor{};
    tensor.data = const_cast<float*>(vector);
    tensor.device = {kDLCPU, 0};
    tensor.ndim = 2;
    tensor.dtype = {kDLFloat, 32, 1};
    tensor.shape = shape;
    return device.Import(tensor, Stream::None(), "xr grab ray");
}

double Round(double value, double step) {
    return step > 0 ? std::round(value / step) * step : value;
}

}

void Grabbing::Snap(const Ref<Scene>& scene, bool last) {
    const GrabSnapping& snap = m_snapping;
    if (snap.turnDegrees > 0) {
        const Double3 heading = Rotate(m_rotation, {1, 0, 0});
        if (std::hypot(heading.x, heading.y) > 1e-6) {
            const double degrees = std::atan2(heading.y, heading.x) * 180 / 3.14159265358979323846;
            const double turn = Round(degrees, snap.turnDegrees) - degrees;
            m_rotation = Normalize(Multiply(FromAxisAngle({0, 0, 1}, turn), m_rotation));
        }
    }
    if (snap.grid > 0) {
        m_position = {Round(m_position.x, snap.grid), Round(m_position.y, snap.grid),
                      snap.surface > 0 ? m_position.z : Round(m_position.z, snap.grid)};
    }
    if (snap.surface > 0) {
        if (!m_caster) {
            m_caster = std::make_shared<RayCaster>(scene);
        }
        const auto read = [&] {
            m_drop->done.Wait();
            m_ground.reset();
            if (m_drop->hit.ToHost<uint32_t>()[0] != 0) {
                m_ground = m_dropFrom - double(m_drop->distance.ToHost<float>()[0]);
            }
            m_drop.reset();
        };
        if (m_drop && m_drop->done.Ready()) {
            read();
        }
        device::Device& device = scene->GetDevice();
        const float origin[3] = {float(m_position.x), float(m_position.y),
                                 float(m_position.z + snap.surface)};
        const float down[3] = {0, 0, -1};
        Rays rays;
        rays.origins = Upload(device, origin);
        rays.directions = Upload(device, down);
        rays.maxDistance = float(2 * snap.surface);
        m_drop = std::make_shared<RayHits>(
            m_caster->Cast(rays, {.outputs = RayOutput::Distance | RayOutput::Prim,
                                  .label = "XR/Grab snapping",
                                  .ignore = m_carried}));
        m_dropFrom = m_position.z + snap.surface;
        if (last) {
            read();
        }
        if (m_ground) {
            m_position.z = *m_ground + m_footing;
        }
    }
}

void Grabbing::Hold(const Pose& hand) {
    const Quatd back = Conjugate(hand.orientation);
    m_offset = Rotate(back, Add(m_position, Scale(hand.position, -1)));
    m_turn = Multiply(back, m_rotation);
}

std::optional<Grabbing::Moved> Grabbing::Update(Rig& rig, const Frame& frame,
                                                const Pointing& pointing) {
    CHRONO_BATCH_RENDER_ZONE("XR/Grabbing");
    Scene& scene = *rig.GetScene();
    const Side main = frame.dominantHand;
    const Side off = main == Side::Right ? Side::Left : Side::Right;
    const Controller& controller = frame.Controller(main);
    const Controller& other = frame.Controller(off);
    const bool tracked = controller.active && controller.pose.valid;
    const bool otherTracked = other.active && other.pose.valid;
    const auto squeeze = [](bool down, float grip) {
        return down ? grip >= kLetGo : grip > kSqueezed;
    };
    const bool squeezed = squeeze(m_down, tracked ? controller.grip : 0.0f);
    const bool started = squeezed && !m_down;
    m_down = squeezed;
    m_otherDown = squeeze(m_otherDown, otherTracked ? other.grip : 0.0f);

    if (started) {
        const std::optional<PointerTarget>& target = pointing.Target();
        std::optional<Prim> prim;
        if (target && !target->overlay && target->prim != kNoPrim) {
            prim = Movable(scene, target->prim);
        }
        if (prim) {
            std::vector<std::string> paths = {prim->Path()};
            const std::optional<Prim> parent = ParentFrame(*prim);
            const bool framed = parent.has_value();
            if (framed) {
                paths.push_back(parent->Path());
            }
            m_carried.clear();
            m_drop.reset();
            m_ground.reset();
            Gather(*prim, m_carried);
            m_footing = 0;
            if (m_snapping.surface > 0) {
                const std::vector<Box> bounds =
                    scene.PrimBounds(std::vector<std::string>{prim->Path()});
                if (!bounds.empty() && bounds[0].upper.z >= bounds[0].lower.z) {
                    m_footing = ReadPlaced(scene, {prim->Path()})[0].origin.z - bounds[0].lower.z;
                }
            }
            const std::vector<Placed> worlds = ReadPlaced(scene, paths);
            m_position = worlds[0].origin;
            m_rotation = worlds[0].turn;
            if (framed) {
                m_parentOrigin = worlds[1].origin;
                m_parentTurn = worlds[1].turn;
                m_parentScale = worlds[1].scale;
            } else {
                m_parentOrigin = {0, 0, 0};
                m_parentTurn = {0, 0, 0, 1};
                m_parentScale = {1, 1, 1};
            }
            Hold(rig.WorldPose(controller.pose));
            m_held = prim->Path();
            m_twoHanded = false;
            const Double3 translate = prim->Get(Xformable::kTranslate);
            const Quatd orient = prim->Get(Xformable::kOrient);
            const Float3 scale = prim->Get(Xformable::kScale);
            m_move = {prim->Path(), translate, orient, scale, translate, orient, scale};
        }
    }

    if (!m_held) {
        return std::nullopt;
    }
    const std::optional<Prim> prim = scene.Find(*m_held);
    if (!prim) {
        m_held.reset();
        m_twoHanded = false;
        return std::nullopt;
    }
    const bool both = tracked && otherTracked && m_otherDown;
    if (both && !m_twoHanded) {
        const Double3 a = rig.WorldPose(controller.pose).position;
        const Double3 b = rig.WorldPose(other.pose).position;
        m_startMiddle = Scale(Add(a, b), 0.5);
        m_startSpan = Add(b, Scale(a, -1));
        m_startPosition = m_position;
        m_startRotation = m_rotation;
        m_startScale = m_move.scale;
        m_twoHanded = Length(m_startSpan) > 1e-3;
    } else if (!both && m_twoHanded && tracked) {
        m_twoHanded = false;
        Hold(rig.WorldPose(controller.pose));
    }
    if (m_twoHanded) {
        const Double3 a = rig.WorldPose(controller.pose).position;
        const Double3 b = rig.WorldPose(other.pose).position;
        const Double3 middle = Scale(Add(a, b), 0.5);
        const Double3 span = Add(b, Scale(a, -1));
        const double grow = std::clamp(Length(span) / Length(m_startSpan), 0.02, 50.0);
        const Quatd turn = Between(m_startSpan, span);
        m_position =
            Add(middle, Rotate(turn, Scale(Add(m_startPosition, Scale(m_startMiddle, -1)), grow)));
        m_rotation = Normalize(Multiply(turn, m_startRotation));
        m_move.scale = {float(m_startScale.x * grow), float(m_startScale.y * grow),
                        float(m_startScale.z * grow)};
        prim->Set(Xformable::kScale, m_move.scale);
    } else if (tracked) {
        const Pose hand = rig.WorldPose(controller.pose);
        m_position = Add(hand.position, Rotate(hand.orientation, m_offset));
        m_rotation = Multiply(hand.orientation, m_turn);
    }
    if (tracked || m_twoHanded) {
        Snap(rig.GetScene(), !squeezed);
        const Quatd up = Conjugate(m_parentTurn);
        const Double3 local = Rotate(up, Add(m_position, Scale(m_parentOrigin, -1)));
        m_move.translate = {local.x / m_parentScale.x, local.y / m_parentScale.y,
                            local.z / m_parentScale.z};
        m_move.orient = Normalize(Multiply(up, m_rotation));
        prim->Set(Xformable::kTranslate, m_move.translate);
        prim->Set(Xformable::kOrient, m_move.orient);
    }
    if (squeezed) {
        return std::nullopt;
    }
    m_held.reset();
    m_twoHanded = false;
    return m_move;
}
}
