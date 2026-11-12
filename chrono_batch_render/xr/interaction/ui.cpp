// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/xr/ui.h"
#include "chrono_batch_render/util/profiler.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fmt/format.h>

#include "chrono_batch_render/xr/poses.h"

namespace chrono::batch_render::xr {
namespace {

constexpr double kPad = 0.006;

double Dot(Double3 a, Double3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Double3 Cross(Double3 a, Double3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Double3 Unit(Double3 v) {
    const double length = std::sqrt(Dot(v, v));
    return length > 1e-9 ? Scale(v, 1 / length) : Double3{0, 0, 0};
}

Float3 Narrow(Double3 v) {
    return {float(v.x), float(v.y), float(v.z)};
}

constexpr double kHandGap = 0.05;

Pose HandOffset() {
    return {{0, 0.10, 0.06}, FromAxes({1, 0, 0}, {0, -0.447, -0.894}, {0, 0.894, -0.447}), true};
}

Pose HeadOffset() {
    return {{0, -0.1, -0.5}, FromAxes({1, 0, 0}, {0, 0, -1}, {0, 1, 0}), true};
}

}

SpatialUi::SpatialUi(UiStyle look) : style(std::move(look)) {}

void SpatialUi::Begin(Rig& rig, const Frame& frame, const Pointing& pointing,
                      const std::vector<PointerEvent>& events) {
    CHRONO_BATCH_RENDER_ZONE("XR/UI");
    m_rig = &rig;
    m_frame = &frame;
    m_pointing = &pointing;
    m_overlay = Overlay();
    m_clicked = 0;
    m_wasTouching = std::exchange(m_touching, {});
    m_near.clear();
    m_pokes.clear();
    m_usedTexts.clear();
    m_tips.clear();
    if (style.touch) {
        for (const Side side : {Side::Left, Side::Right}) {
            const HandJoints& hand = frame.Hand(side);
            const Controller& controller = frame.Controller(side);
            if (hand.active && hand[HandJoint::IndexTip].pose.valid) {
                m_tips.push_back({side, rig.WorldPose(hand[HandJoint::IndexTip].pose).position});
            } else if (controller.active && controller.aim.valid) {
                m_tips.push_back({side, rig.WorldPose(controller.aim).position});
            }
        }
    }
    const auto ours = [&](const PointerTarget& target) {
        return target.overlay && target.overlay->overlay == style.layer ? target.overlay->id : 0u;
    };
    const std::optional<PointerTarget>& target = pointing.Target();
    m_hovered = target ? ours(*target) : 0;
    for (const PointerEvent& event : events) {
        if (event.gesture == PointerGesture::Press) {
            m_pressed = ours(event.target);
        } else if (event.gesture == PointerGesture::Release) {
            m_pressed = 0;
        } else if (event.gesture == PointerGesture::Click) {
            m_clicked = ours(event.target);
        }
    }
}

bool SpatialUi::BeginPanel(const PanelDesc& desc) {
    m_desc = desc;
    m_rows = 0;
    m_placed.reset();
    m_panel = Id("");
    if (m_rig == nullptr || m_frame == nullptr) {
        return false;
    }
    Pose pose;
    Double3 hand{};
    switch (desc.mount) {
    case PanelMount::World:
        pose = desc.pose.value_or(Pose{{0, 1, 1.5}, {0, 0, 0, 1}, true});
        break;
    case PanelMount::Hand: {
        const Controller& controller = m_frame->Controller(desc.hand);
        if (!controller.active || !controller.pose.valid) {
            return false;
        }
        hand = m_rig->WorldPose(controller.pose).position;
        pose = Offset(m_rig->WorldPose(controller.pose), desc.pose.value_or(HandOffset()));
        break;
    }
    case PanelMount::Head:
        if (!m_frame->head.valid) {
            return false;
        }
        pose = Offset(m_rig->WorldPose(m_frame->head), desc.pose.value_or(HeadOffset()));
        break;
    }
    Placed placed{pose.position, Rotate(pose.orientation, {1, 0, 0}),
                  Rotate(pose.orientation, {0, 0, 1}), Rotate(pose.orientation, {0, -1, 0})};
    if (desc.mount == PanelMount::World && desc.facing) {
        Double3 toward = Add(m_rig->WorldPose(m_frame->head).position, Scale(pose.position, -1));
        toward.z = 0;
        if (Dot(toward, toward) > 1e-9) {
            placed.normal = Unit(toward);
            placed.up = {0, 0, 1};
            placed.right = Unit(Cross(Scale(placed.normal, -1), placed.up));
        }
    }
    const auto last = m_lastRows.find(m_panel);
    const double height =
        last != m_lastRows.end() ? kPad + last->second * (desc.rowHeight + kPad) : 0.0;
    if (desc.mount == PanelMount::Hand && desc.facing && !desc.pose) {
        const Double3 head = m_rig->WorldPose(m_frame->head).position;
        const Double3 toward = Add(head, Scale(hand, -1));
        if (m_frame->head.valid && Dot(toward, toward) > 1e-9) {
            const Double3 normal = Unit(toward);
            Double3 up = Add(Double3{0, 0, 1}, Scale(normal, -normal.z));
            up = Dot(up, up) > 1e-9 ? Unit(up) : placed.up;
            placed.normal = normal;
            placed.up = up;
            placed.right = Unit(Cross(Scale(normal, -1), up));
            placed.top = Add(hand, Scale(up, kHandGap + height));
        }
    }
    m_placed = placed;
    if (last != m_lastRows.end() && last->second > 0) {
        const Double3 half = Scale(placed.right, desc.width / 2);
        const Double3 down = Scale(placed.up, -height);
        const Double3 a = Add(placed.top, Scale(half, -1));
        const Double3 b = Add(placed.top, half);
        const std::vector<Float3> corners = {Narrow(a), Narrow(b), Narrow(Add(b, down)),
                                             Narrow(Add(a, down))};
        OverlayStyle back;
        back.color = style.panel;
        back.id = m_panel;
        m_overlay.Polygon(corners, back);
    }
    return true;
}

void SpatialUi::EndPanel() {
    m_lastRows[m_panel] = m_placed ? m_rows : 0;
    m_placed.reset();
}

void SpatialUi::End(Overlays& overlays) {
    CHRONO_BATCH_RENDER_ZONE("XR/UI");
    std::erase_if(m_texts, [&](const auto& entry) { return !m_usedTexts.contains(entry.first); });
    overlays.Set(style.layer, std::move(m_overlay));
    m_overlay = Overlay();
    m_rig = nullptr;
    m_frame = nullptr;
    m_pointing = nullptr;
}

bool SpatialUi::Hovered() const {
    return m_hovered != 0;
}

uint32_t SpatialUi::Id(std::string_view label) const {
    uint32_t hash = 2166136261u;
    const auto mix = [&](std::string_view text) {
        for (const char c : text) {
            hash = (hash ^ uint8_t(c)) * 16777619u;
        }
    };
    mix(m_desc.name);
    mix("/");
    mix(label);
    return hash | 1u;
}

Double3 SpatialUi::Corner(uint32_t row, float across, float down) const {
    const Placed& placed = *m_placed;
    const double inner = m_desc.width - 2 * kPad;
    const double x = -m_desc.width / 2 + kPad + across * inner;
    const double y = kPad + row * (m_desc.rowHeight + kPad) + down * m_desc.rowHeight;
    return Add(Add(placed.top, Scale(placed.right, x)), Scale(placed.up, -y));
}

void SpatialUi::Rect(uint32_t row, float from, float to, const Float4& color, uint32_t id,
                     float lift) {
    const Double3 raise = Scale(m_placed->normal, lift);
    const std::vector<Float3> corners = {
        Narrow(Add(Corner(row, from, 0), raise)), Narrow(Add(Corner(row, to, 0), raise)),
        Narrow(Add(Corner(row, to, 1), raise)), Narrow(Add(Corner(row, from, 1), raise))};
    OverlayStyle shape;
    shape.color = color;
    shape.id = id;
    shape.pickable = id != 0;
    m_overlay.Polygon(corners, shape);
}

void SpatialUi::Label(uint32_t row, float at, float align, std::string_view text) {
    if (style.flatText && !text.empty() && m_rig != nullptr) {
        const float pixels = style.textSize * style.textPixelsPerMeter;
        std::string key = fmt::format("{}|{}", pixels, text);
        auto found = m_texts.find(key);
        if (found == m_texts.end()) {
            found = m_texts
                        .emplace(key, RenderOverlayText(m_rig->GetScene()->GetDevice(), text,
                                                        pixels, style.text))
                        .first;
        }
        m_usedTexts.insert(key);
        const device::Tensor& image = found->second;
        const double perPixel = 1.0 / style.textPixelsPerMeter;
        const double width = double(image.Shape()[1]) * perPixel;
        const double height = double(image.Shape()[0]) * perPixel;
        const Placed& placed = *m_placed;
        const Double3 anchor = Add(Corner(row, at, 0.5f), Scale(placed.normal, 0.004));
        const Double3 corner =
            Add(Add(anchor, Scale(placed.right, -align * width)), Scale(placed.up, height / 2));
        OverlayStyle flat;
        flat.pickable = false;
        m_overlay.Image(Narrow(corner), Narrow(Scale(placed.right, width)),
                        Narrow(Scale(placed.up, -height)), image, flat);
        return;
    }
    OverlayStyle label;
    label.color = style.text;
    label.size = style.textSize;
    label.units = OverlayUnits::Meters;
    label.background = {0, 0, 0, 0};
    label.align = {align, 0.5f};
    label.pickable = false;
    const Double3 point = Add(Corner(row, at, 0.5f), Scale(m_placed->normal, 0.004));
    m_overlay.Label(Narrow(point), text, label);
}

Float4 SpatialUi::Shade(uint32_t id) const {
    if (m_pressed == id || m_touching.contains(id)) {
        return style.active;
    }
    return m_hovered == id || m_near.contains(id) ? style.hovered : style.widget;
}

std::optional<float> SpatialUi::Touch(uint32_t row, uint32_t id) {
    const Placed& placed = *m_placed;
    const Double3 origin = Corner(row, 0, 0);
    const double inner = m_desc.width - 2 * kPad;
    const bool held = m_wasTouching.contains(id);
    for (const Fingertip& tip : m_tips) {
        if (m_desc.mount == PanelMount::Hand && tip.hand == m_desc.hand) {
            continue;
        }
        const Double3 offset = Add(tip.at, Scale(origin, -1));
        const double across = Dot(offset, placed.right) / inner;
        const double down = -Dot(offset, placed.up) / m_desc.rowHeight;
        const double depth = Dot(Add(tip.at, Scale(placed.top, -1)), placed.normal) - 0.002;
        if (across < 0 || across > 1 || down < 0 || down > 1) {
            continue;
        }
        if (depth < 0.05) {
            m_near.insert(id);
        }
        if (depth > -0.03 && depth < (held ? 0.015 : 0.005)) {
            m_touching.insert(id);
            if (!held) {
                m_pokes.push_back(tip.hand);
            }
            return float(across);
        }
    }
    return std::nullopt;
}

void SpatialUi::Text(std::string_view text) {
    if (m_placed) {
        Label(m_rows++, 0, 0, text);
    }
}

bool SpatialUi::Button(std::string_view label) {
    const uint32_t id = Id(label);
    if (!m_placed) {
        return false;
    }
    const bool poked = Touch(m_rows, id) && !m_wasTouching.contains(id);
    Rect(m_rows, 0, 1, Shade(id), id);
    Label(m_rows, 0.5f, 0.5f, label);
    ++m_rows;
    return m_clicked == id || poked;
}

bool SpatialUi::Checkbox(std::string_view label, bool& value) {
    const uint32_t id = Id(label);
    if (!m_placed) {
        return false;
    }
    const bool poked = Touch(m_rows, id) && !m_wasTouching.contains(id);
    const bool clicked = m_clicked == id || poked;
    if (clicked) {
        value = !value;
    }
    Rect(m_rows, 0, 1, m_hovered == id || m_near.contains(id) ? style.hovered : style.widget, id);
    const float box = float(m_desc.rowHeight * 0.6 / (m_desc.width - 2 * kPad));
    Rect(m_rows, 1 - box - 0.03f, 1 - 0.03f, value ? style.active : style.panel, 0, 0.003f);
    Label(m_rows, 0.03f, 0, label);
    ++m_rows;
    return clicked;
}

bool SpatialUi::Slider(std::string_view label, float& value, float min, float max) {
    const uint32_t id = Id(label);
    if (!m_placed || !(max > min)) {
        return false;
    }
    bool changed = false;
    if (const std::optional<float> pushed = Touch(m_rows, id)) {
        const float next = min + std::clamp(*pushed, 0.0f, 1.0f) * (max - min);
        changed = next != value;
        value = next;
    } else if (m_pressed == id && m_pointing != nullptr && m_pointing->Aimed()) {
        const Placed& placed = *m_placed;
        const Double3 origin = m_pointing->Origin();
        const Double3 direction = m_pointing->Direction();
        const double facing = Dot(direction, placed.normal);
        if (std::abs(facing) > 1e-6) {
            const double t = Dot(Add(placed.top, Scale(origin, -1)), placed.normal) / facing;
            if (t > 0) {
                const Double3 hit = Add(origin, Scale(direction, t));
                const double across = Dot(Add(hit, Scale(Corner(m_rows, 0, 0), -1)), placed.right) /
                                      (m_desc.width - 2 * kPad);
                const float next = min + float(std::clamp(across, 0.0, 1.0)) * (max - min);
                changed = next != value;
                value = next;
            }
        }
    }
    const float share = std::clamp((value - min) / (max - min), 0.0f, 1.0f);
    Rect(m_rows, 0, 1, Shade(id), id);
    if (share > 0) {
        Rect(m_rows, 0, share, style.active, 0, 0.003f);
    }
    Label(m_rows, 0.03f, 0, fmt::format("{}  {:.2f}", label, value));
    ++m_rows;
    return changed;
}

}
