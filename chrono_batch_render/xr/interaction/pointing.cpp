// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/poses.h"
#include "chrono_batch_render/xr/rig.h"

namespace chrono::batch_render::xr {
namespace {

constexpr uint32_t kSamples = 24;
constexpr double kNearest = 0.05;
constexpr double kSameSurface = 0.03;
constexpr float kPressed = 0.6f;
constexpr float kLetGo = 0.4f;

Double3 Subtract(Double3 a, Double3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double Dot(Double3 a, Double3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double Length(Double3 v) {
    return std::sqrt(Dot(v, v));
}

std::optional<Float2> Project(const Pose& eye, const Fov& fov, Uint2 size, Double3 point) {
    const Double3 local = Rotate(Conjugate(eye.orientation), Subtract(point, eye.position));
    if (local.z > -1e-3) {
        return std::nullopt;
    }
    const double x = local.x / -local.z;
    const double y = local.y / -local.z;
    const double left = std::tan(fov.left);
    const double right = std::tan(fov.right);
    const double up = std::tan(fov.up);
    const double down = std::tan(fov.down);
    return Float2{float((x - left) / (right - left) * size.x),
                  float((up - y) / (up - down) * size.y)};
}

bool Inside(Float2 pixel, Uint2 size) {
    return pixel.x >= 0 && pixel.y >= 0 && pixel.x < float(size.x) && pixel.y < float(size.y);
}

}

bool PointerTarget::Same(const PointerTarget& other) const {
    if (overlay.has_value() != other.overlay.has_value()) {
        return false;
    }
    if (overlay) {
        return overlay->overlay == other.overlay->overlay && overlay->id == other.overlay->id &&
               overlay->batch == other.overlay->batch && overlay->item == other.overlay->item;
    }
    return prim == other.prim;
}

std::vector<PointerEvent> Pointing::Update(Rig& rig, Overlays& overlays, const Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Pointing");
    const Side hand = frame.dominantHand;
    const Controller& controller = frame.Controller(hand);
    std::optional<PointerTarget> target;
    double reach = 0;
    m_aimed = controller.active && controller.aim.valid;
    if (m_aimed) {
        const Pose aim = rig.WorldPose(controller.aim);
        m_origin = aim.position;
        m_direction = Rotate(aim.orientation, {0, 0, -1});
        const double scale = rig.UserScale();
        const PointerHit laser = rig.Laser(hand);
        reach = (laser.hit ? laser.distance : double(rig.GetPointers().maxDistance)) * scale;
        if (laser.hit && laser.prim != kNoPrim) {
            target = PointerTarget{.prim = laser.prim,
                                   .position = rig.WorldPose({laser.localPosition}).position,
                                   .distance = laser.distance * scale};
        }
    }

    const bool answered =
        std::ranges::all_of(m_samples, [](const Sample& sample) { return sample.pick.Ready(); });
    if (answered) {
        struct Candidate {
            PointerTarget target;
            double off;
        };

        std::vector<Candidate> candidates;
        double first = std::numeric_limits<double>::infinity();
        for (const Sample& sample : m_samples) {
            const std::optional<OverlayHit> hit = sample.pick.Hit();
            if (!hit) {
                continue;
            }
            const Double3 found{hit->position.x, hit->position.y, hit->position.z};
            const Double3 offset = Subtract(found, sample.origin);
            const double along = Dot(offset, sample.direction);
            const double off = Length(Subtract(offset, Scale(sample.direction, along)));
            if (along < 0 || along > sample.reach || off > 0.02 + 0.02 * along) {
                continue;
            }
            first = std::min(first, along);
            candidates.push_back(
                {{.prim = hit->prim, .overlay = hit, .position = found, .distance = along}, off});
        }
        std::optional<PointerTarget> nearest;
        double closest = std::numeric_limits<double>::infinity();
        for (const Candidate& candidate : candidates) {
            if (candidate.target.distance <= first + kSameSurface && candidate.off < closest) {
                closest = candidate.off;
                nearest = candidate.target;
            }
        }
        m_samples.clear();
        if (nearest && (!target || nearest->distance < target->distance)) {
            target = nearest;
        }
    } else if (m_target && m_target->overlay && controller.active) {
        target = m_target;
    }

    if (answered && controller.active && controller.aim.valid && reach > kNearest) {
        const View& view = frame.Eye(Side::Left);
        const Pose eye = rig.WorldPose(view.pose);
        const Uint2 size = rig.RecommendedResolution();
        std::vector<std::pair<double, Float2>> points;
        for (uint32_t k = 0; k < kSamples; ++k) {
            const double along = kNearest * std::pow(reach / kNearest, double(k) / (kSamples - 1));
            if (const std::optional<Float2> pixel =
                    Project(eye, view.fov, size, Add(m_origin, Scale(m_direction, along)))) {
                points.emplace_back(along, *pixel);
            }
        }
        if (m_target && m_target->overlay) {
            for (const double scale : {0.97, 1.0, 1.03}) {
                const double along = std::min(m_target->distance * scale, reach);
                if (const std::optional<Float2> pixel =
                        Project(eye, view.fov, size, Add(m_origin, Scale(m_direction, along)));
                    pixel && Inside(*pixel, size)) {
                    m_samples.push_back(
                        {overlays.Pick(rig.Product(), *pixel, 2.0f), m_origin, m_direction, reach});
                }
            }
        }
        for (size_t k = 0; k < points.size(); ++k) {
            const Float2 pixel = points[k].second;
            if (!Inside(pixel, size)) {
                continue;
            }
            const Float2 next = points[std::min(k + 1, points.size() - 1)].second;
            const float gap = std::hypot(next.x - pixel.x, next.y - pixel.y);
            const float radius = std::clamp(0.6f * gap, 3.0f, 48.0f);
            m_samples.push_back(
                {overlays.Pick(rig.Product(), pixel, radius), m_origin, m_direction, reach});
        }
    }

    std::vector<PointerEvent> events;
    const auto send = [&](PointerGesture gesture, const std::optional<PointerTarget>& at) {
        events.push_back({gesture, hand, at.value_or(PointerTarget{}), m_origin, m_direction});
    };
    const bool changed =
        target.has_value() != m_target.has_value() || (target && !target->Same(*m_target));
    if (changed) {
        if (m_target) {
            send(PointerGesture::Leave, m_target);
        }
        if (target) {
            send(PointerGesture::Hover, target);
        }
    }
    m_target = target;
    const float trigger = controller.active ? controller.trigger : 0.0f;
    if (!m_down && trigger > kPressed) {
        m_down = true;
        m_pressed = m_target;
        send(PointerGesture::Press, m_target);
    } else if (m_down && trigger < kLetGo) {
        m_down = false;
        send(PointerGesture::Release, m_target);
        const bool same = m_pressed.has_value() == m_target.has_value() &&
                          (!m_target || m_target->Same(*m_pressed));
        if (same) {
            send(PointerGesture::Click, m_target);
        }
        m_pressed.reset();
    }
    return events;
}

void Pointing::Draw(Overlays& overlays, const RigVisuals& visuals) const {
    Overlay overlay;
    if (m_target) {
        OverlayStyle style;
        style.color = visuals.color;
        style.pickable = false;
        if (!m_target->overlay && m_target->prim != kNoPrim) {
            overlay.Outline(std::vector<uint32_t>{m_target->prim}, style);
        }
        style.size = 10;
        style.depth = OverlayDepth::Always;
        const Double3 at = m_target->position;
        overlay.Marker({float(at.x), float(at.y), float(at.z)}, style);
    }
    overlays.Set(visuals.layer + " pointing", std::move(overlay));
}

void Buzz(Frame& frame, const std::vector<PointerEvent>& events, const PointerHaptics& feel) {
    for (const PointerEvent& event : events) {
        const bool found = event.target.overlay || event.target.prim != kNoPrim;
        if (event.gesture == PointerGesture::Hover && found && feel.hover > 0) {
            frame.Haptics(event.hand, feel.hover, feel.duration);
        } else if (event.gesture == PointerGesture::Press && feel.press > 0) {
            frame.Haptics(event.hand, feel.press, feel.duration);
        }
    }
}

}
