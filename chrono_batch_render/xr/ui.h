// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "chrono_batch_render/overlay/overlay.h"
#include "chrono_batch_render/xr/frame.h"
#include "chrono_batch_render/xr/rig.h"
#include "chrono_batch_render/xr_export.h"

namespace chrono::batch_render::xr {

enum class PanelMount : uint32_t { World, Hand, Head };

struct PanelDesc {
    std::string name;
    PanelMount mount = PanelMount::World;
    std::optional<Pose> pose;
    Side hand = Side::Left;
    float width = 0.3f;
    float rowHeight = 0.045f;
    bool facing = true;
};

struct UiStyle {
    Float4 panel = {0.08f, 0.09f, 0.11f, 0.88f};
    Float4 widget = {0.2f, 0.22f, 0.26f, 1};
    Float4 hovered = {0.27f, 0.4f, 0.58f, 1};
    Float4 active = {0.3f, 0.72f, 1, 1};
    Float4 text = {1, 1, 1, 1};
    float textSize = 0.016f;
    std::string layer = "xr ui";
    bool flatText = true;
    float textPixelsPerMeter = 2400;
    bool touch = true;
};

class CHRONO_BATCH_RENDER_XR_API SpatialUi {
  public:
    explicit SpatialUi(UiStyle look = {});

    void Begin(Rig& rig, const Frame& frame, const Pointing& pointing,
               const std::vector<PointerEvent>& events);
    bool BeginPanel(const PanelDesc& desc);
    void Text(std::string_view text);
    bool Button(std::string_view label);
    bool Checkbox(std::string_view label, bool& value);
    bool Slider(std::string_view label, float& value, float min, float max);
    void EndPanel();
    void End(Overlays& overlays);

    bool Hovered() const;

    const std::vector<Side>& Pokes() const {
        return m_pokes;
    }

    UiStyle style;

  private:
    struct Placed {
        Double3 top;
        Double3 right;
        Double3 up;
        Double3 normal;
    };

    uint32_t Id(std::string_view label) const;
    void Rect(uint32_t row, float from, float to, const Float4& color, uint32_t id,
              float lift = 0.002f);
    void Label(uint32_t row, float at, float align, std::string_view text);
    Float4 Shade(uint32_t id) const;
    Double3 Corner(uint32_t row, float across, float down) const;
    std::optional<float> Touch(uint32_t row, uint32_t id);

    struct Fingertip {
        Side hand;
        Double3 at;
    };

    Rig* m_rig = nullptr;
    const Frame* m_frame = nullptr;
    const Pointing* m_pointing = nullptr;
    Overlay m_overlay;
    std::optional<Placed> m_placed;
    PanelDesc m_desc;
    uint32_t m_panel = 0;
    uint32_t m_rows = 0;
    uint32_t m_hovered = 0;
    uint32_t m_clicked = 0;
    uint32_t m_pressed = 0;
    std::map<uint32_t, uint32_t> m_lastRows;
    std::vector<Fingertip> m_tips;
    std::set<uint32_t> m_touching;
    std::set<uint32_t> m_wasTouching;
    std::set<uint32_t> m_near;
    std::vector<Side> m_pokes;
    std::map<std::string, device::Tensor, std::less<>> m_texts;
    std::set<std::string, std::less<>> m_usedTexts;
};

}
