// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/theme/theme.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>

#include "chrono_batch_render/scene/scene.h"

namespace chrono::batch_render::app {
namespace {

constexpr float kPi = 3.14159265f;
const ImU32 kLamp = IM_COL32(226, 220, 132, 255);

void SetMetrics(const Theme& theme, ImGuiStyle& style) {
    style.WindowPadding = {8.0f, 6.0f};
    style.FramePadding = theme.framePadding;
    style.ItemSpacing = theme.itemSpacing;
    style.ItemInnerSpacing = {4.0f, 3.0f};
    style.CellPadding = {4.0f, 2.0f};
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 8.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = theme.rounding;
    style.PopupRounding = theme.rounding;
    style.ScrollbarRounding = theme.rounding;
    style.GrabRounding = theme.rounding;
    style.TabRounding = theme.rounding;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = theme.frameBorder;
    style.TabBorderSize = theme.frameBorder;
    style.TabBarOverlineSize = 0.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowTitleAlign = {0.0f, 0.5f};
    style.SeparatorTextBorderSize = 1.0f;
}

void SetColors(const Theme& theme, ImGuiStyle& style) {
    auto set = [&](ImGuiCol index, const ImVec4& color) { style.Colors[index] = color; };
    ImVec4 accentDim = WithAlpha(theme.accent, 0.45f);
    ImVec4 accentFaint = WithAlpha(theme.accent, 0.25f);
    ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    set(ImGuiCol_Text, theme.text);
    set(ImGuiCol_TextDisabled, theme.textMuted);
    set(ImGuiCol_WindowBg, theme.panel);
    set(ImGuiCol_ChildBg, clear);
    set(ImGuiCol_PopupBg, WithAlpha(theme.overlay, 1.0f));
    set(ImGuiCol_Border, theme.border);
    set(ImGuiCol_BorderShadow, clear);
    set(ImGuiCol_FrameBg, theme.field);
    set(ImGuiCol_FrameBgHovered, theme.fieldHovered);
    set(ImGuiCol_FrameBgActive, theme.fieldHovered);
    for (ImGuiCol title : {ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed,
                           ImGuiCol_MenuBarBg}) {
        set(title, theme.background);
    }
    set(ImGuiCol_ScrollbarBg, clear);
    set(ImGuiCol_ScrollbarGrab, theme.fieldHovered);
    set(ImGuiCol_ScrollbarGrabHovered, theme.textMuted);
    set(ImGuiCol_ScrollbarGrabActive, theme.accent);
    set(ImGuiCol_CheckMark, theme.checkMark);
    set(ImGuiCol_CheckboxSelectedBg, theme.checkBox);
    set(ImGuiCol_SliderGrab, theme.buttonLit);
    set(ImGuiCol_SliderGrabActive, theme.text);
    set(ImGuiCol_Button, theme.button);
    set(ImGuiCol_ButtonHovered, theme.fieldHovered);
    set(ImGuiCol_ButtonActive, theme.buttonLit);
    set(ImGuiCol_Header, theme.selection);
    set(ImGuiCol_HeaderHovered, WithAlpha(theme.selection, 0.85f));
    set(ImGuiCol_HeaderActive, theme.selection);
    set(ImGuiCol_Separator, theme.separator);
    set(ImGuiCol_SeparatorHovered, accentDim);
    set(ImGuiCol_SeparatorActive, theme.accent);
    set(ImGuiCol_ResizeGrip, clear);
    set(ImGuiCol_ResizeGripHovered, accentDim);
    set(ImGuiCol_ResizeGripActive, theme.accent);
    set(ImGuiCol_InputTextCursor, theme.text);
    set(ImGuiCol_Tab, theme.background);
    set(ImGuiCol_TabHovered, theme.fieldHovered);
    set(ImGuiCol_TabSelected, theme.panel);
    set(ImGuiCol_TabSelectedOverline, theme.accent);
    set(ImGuiCol_TabDimmed, theme.background);
    set(ImGuiCol_TabDimmedSelected, theme.panel);
    set(ImGuiCol_TabDimmedSelectedOverline, WithAlpha(theme.accent, 0.0f));
    set(ImGuiCol_DockingPreview, accentDim);
    set(ImGuiCol_DockingEmptyBg, theme.background);
    set(ImGuiCol_PlotLines, theme.accent);
    set(ImGuiCol_PlotLinesHovered, theme.text);
    set(ImGuiCol_PlotHistogram, theme.accent);
    set(ImGuiCol_PlotHistogramHovered, theme.text);
    set(ImGuiCol_TableHeaderBg, theme.header);
    set(ImGuiCol_TableBorderStrong, theme.separator);
    set(ImGuiCol_TableBorderLight, theme.separator);
    set(ImGuiCol_TableRowBg, clear);
    set(ImGuiCol_TableRowBgAlt, WithAlpha(theme.text, 0.03f));
    set(ImGuiCol_TextLink, theme.accent);
    set(ImGuiCol_TextSelectedBg, accentFaint);
    set(ImGuiCol_TreeLines, theme.separator);
    set(ImGuiCol_DragDropTarget, theme.accent);
    set(ImGuiCol_DragDropTargetBg, accentFaint);
    set(ImGuiCol_UnsavedMarker, theme.warning);
    set(ImGuiCol_NavCursor, theme.accent);
    set(ImGuiCol_NavWindowingHighlight, theme.accent);
    set(ImGuiCol_NavWindowingDimBg, WithAlpha(theme.background, 0.6f));
    set(ImGuiCol_ModalWindowDimBg, WithAlpha(theme.background, 0.6f));
}

ImU32 Rgb(int r, int g, int b) {
    return IM_COL32(r, g, b, 255);
}

struct Frame {
    ImVec2 corner;
    float size;

    ImVec2 operator()(float u, float v) const {
        return {corner.x + u * size, corner.y + v * size};
    }

    float operator()(float length) const {
        return length * size;
    }
};

void Tripod(ImDrawList* draw, const Frame& at) {
    float width = std::max(1.2f, at(0.09f));
    ImVec2 center = at(0.5f, 0.58f);
    draw->AddLine(center, at(0.5f, 0.1f), Rgb(110, 200, 90), width);
    draw->AddLine(center, at(0.1f, 0.86f), Rgb(225, 80, 70), width);
    draw->AddLine(center, at(0.9f, 0.86f), Rgb(80, 140, 235), width);
}

void Folder(ImDrawList* draw, const Frame& at) {
    draw->AddRectFilled(at(0.08f, 0.2f), at(0.45f, 0.38f), Rgb(74, 85, 88), at(0.06f));
    draw->AddRectFilled(at(0.08f, 0.3f), at(0.92f, 0.82f), Rgb(96, 108, 112), at(0.08f));
}

void Cube(ImDrawList* draw, const Frame& at, ImU32 topColor = Rgb(150, 160, 164)) {
    const ImVec2 top[] = {at(0.5f, 0.1f), at(0.9f, 0.3f), at(0.5f, 0.5f), at(0.1f, 0.3f)};
    const ImVec2 left[] = {at(0.1f, 0.3f), at(0.5f, 0.5f), at(0.5f, 0.92f), at(0.1f, 0.72f)};
    const ImVec2 right[] = {at(0.5f, 0.5f), at(0.9f, 0.3f), at(0.9f, 0.72f), at(0.5f, 0.92f)};
    draw->AddConvexPolyFilled(top, 4, topColor);
    draw->AddConvexPolyFilled(left, 4, Rgb(104, 114, 118));
    draw->AddConvexPolyFilled(right, 4, Rgb(74, 85, 88));
}

void Ball(ImDrawList* draw, const Frame& at, ImU32 base, ImU32 middle, ImU32 highlight) {
    draw->AddCircleFilled(at(0.5f, 0.52f), at(0.4f), base);
    draw->AddCircleFilled(at(0.44f, 0.44f), at(0.28f), middle);
    draw->AddCircleFilled(at(0.38f, 0.36f), at(0.11f), highlight);
}

void Cylinder(ImDrawList* draw, const Frame& at, bool cone) {
    ImU32 side = Rgb(96, 108, 112);
    if (cone) {
        draw->AddTriangleFilled(at(0.5f, 0.08f), at(0.88f, 0.8f), at(0.12f, 0.8f), side);
        draw->AddEllipseFilled(at(0.5f, 0.8f), ImVec2(at(0.38f), at(0.12f)), Rgb(74, 85, 88));
        return;
    }
    draw->AddRectFilled(at(0.15f, 0.26f), at(0.85f, 0.8f), side);
    draw->AddEllipseFilled(at(0.5f, 0.8f), ImVec2(at(0.35f), at(0.12f)), side);
    draw->AddEllipseFilled(at(0.5f, 0.26f), ImVec2(at(0.35f), at(0.12f)), Rgb(150, 160, 164));
}

void Plane(ImDrawList* draw, const Frame& at, bool checkered) {
    const ImVec2 quad[] = {at(0.3f, 0.3f), at(0.95f, 0.3f), at(0.7f, 0.78f), at(0.05f, 0.78f)};
    draw->AddConvexPolyFilled(quad, 4, Rgb(104, 114, 118));
    if (checkered) {
        const ImVec2 a[] = {at(0.3f, 0.3f), at(0.625f, 0.3f), at(0.5f, 0.54f), at(0.175f, 0.54f)};
        const ImVec2 b[] = {at(0.5f, 0.54f), at(0.825f, 0.54f), at(0.7f, 0.78f), at(0.375f, 0.78f)};
        draw->AddConvexPolyFilled(a, 4, Rgb(74, 85, 88));
        draw->AddConvexPolyFilled(b, 4, Rgb(74, 85, 88));
        return;
    }
    draw->AddLine(at(0.5f, 0.3f), at(0.38f, 0.78f), Rgb(150, 160, 164), 1.0f);
    draw->AddLine(at(0.18f, 0.54f), at(0.83f, 0.54f), Rgb(150, 160, 164), 1.0f);
}

void Sun(ImDrawList* draw, const Frame& at) {
    draw->AddCircleFilled(at(0.5f, 0.5f), at(0.2f), kLamp);
    for (int k = 0; k < 8; ++k) {
        float a = static_cast<float>(k) * kPi / 4.0f;
        draw->AddLine(at(0.5f + 0.3f * std::cos(a), 0.5f + 0.3f * std::sin(a)),
                      at(0.5f + 0.44f * std::cos(a), 0.5f + 0.44f * std::sin(a)), kLamp,
                      std::max(1.0f, at(0.07f)));
    }
}

void Bulb(ImDrawList* draw, const Frame& at) {
    draw->AddCircleFilled(at(0.5f, 0.4f), at(0.28f), kLamp);
    draw->AddRectFilled(at(0.38f, 0.64f), at(0.62f, 0.9f), Rgb(150, 160, 164), at(0.04f));
}

void Emitter(ImDrawList* draw, const Frame& at, bool disk) {
    const ImU32 lamp = disk ? Rgb(250, 190, 110) : Rgb(225, 235, 250);
    if (disk) {
        draw->AddEllipseFilled(at(0.5f, 0.32f), ImVec2(at(0.38f), at(0.16f)), lamp);
    } else {
        const ImVec2 quad[] = {at(0.22f, 0.16f), at(0.92f, 0.16f), at(0.78f, 0.46f),
                               at(0.08f, 0.46f)};
        draw->AddConvexPolyFilled(quad, 4, lamp);
    }
    for (const float u : {0.3f, 0.5f, 0.7f}) {
        draw->AddLine(at(u, 0.56f), at(u, 0.9f), lamp, std::max(1.0f, at(0.07f)));
    }
}

void Tube(ImDrawList* draw, const Frame& at) {
    const ImU32 lamp = Rgb(150, 225, 235);
    draw->AddRectFilled(at(0.08f, 0.38f), at(0.92f, 0.62f), lamp, at(0.12f));
    draw->AddRectFilled(at(0.02f, 0.42f), at(0.1f, 0.58f), Rgb(150, 160, 164));
    draw->AddRectFilled(at(0.9f, 0.42f), at(0.98f, 0.58f), Rgb(150, 160, 164));
}

void Dome(ImDrawList* draw, const Frame& at) {
    draw->PathArcTo(at(0.5f, 0.72f), at(0.38f), kPi, 2.0f * kPi, 16);
    draw->PathFillConvex(Rgb(127, 176, 222));
    draw->AddCircleFilled(at(0.62f, 0.5f), at(0.08f), kLamp);
    draw->AddLine(at(0.08f, 0.76f), at(0.92f, 0.76f), Rgb(110, 150, 120),
                  std::max(1.0f, at(0.08f)));
}

void Globe(ImDrawList* draw, const Frame& at) {
    const ImU32 color = Rgb(127, 166, 194);
    const float width = std::max(1.0f, at(0.07f));
    draw->AddCircle(at(0.5f, 0.5f), at(0.4f), color, 0, width);
    draw->AddEllipse(at(0.5f, 0.5f), ImVec2(at(0.16f), at(0.4f)), color, 0, 0, width);
    draw->AddLine(at(0.1f, 0.5f), at(0.9f, 0.5f), color, width);
}

void Backdrop(ImDrawList* draw, const Frame& at, bool gradient) {
    const ImU32 top = gradient ? Rgb(127, 166, 194) : Rgb(104, 114, 118);
    const ImU32 bottom = gradient ? Rgb(206, 198, 214) : Rgb(104, 114, 118);
    draw->AddRectFilledMultiColor(at(0.1f, 0.14f), at(0.9f, 0.86f), top, top, bottom, bottom);
    draw->AddRect(at(0.1f, 0.14f), at(0.9f, 0.86f), Rgb(74, 85, 88), 0.0f,
                  std::max(1.0f, at(0.06f)));
}

void CameraIcon(ImDrawList* draw, const Frame& at) {
    ImU32 body = Rgb(150, 160, 164);
    draw->AddRectFilled(at(0.08f, 0.32f), at(0.66f, 0.78f), body, at(0.08f));
    draw->AddTriangleFilled(at(0.66f, 0.55f), at(0.94f, 0.32f), at(0.94f, 0.78f), body);
    draw->AddCircleFilled(at(0.26f, 0.22f), at(0.1f), body);
    draw->AddCircleFilled(at(0.48f, 0.2f), at(0.12f), body);
}

void Film(ImDrawList* draw, const Frame& at) {
    ImU32 color = Rgb(127, 166, 194);
    draw->AddRect(at(0.1f, 0.18f), at(0.9f, 0.82f), color, at(0.06f), std::max(1.2f, at(0.08f)));
    for (float u : {0.24f, 0.42f, 0.6f, 0.78f}) {
        draw->AddRectFilled(at(u - 0.05f, 0.25f), at(u + 0.05f, 0.32f), color);
        draw->AddRectFilled(at(u - 0.05f, 0.68f), at(u + 0.05f, 0.75f), color);
    }
}

void Aperture(ImDrawList* draw, const Frame& at) {
    ImU32 color = Rgb(127, 166, 194);
    draw->AddCircle(at(0.5f, 0.5f), at(0.38f), color, 0, std::max(1.2f, at(0.08f)));
    for (int k = 0; k < 6; ++k) {
        float a = static_cast<float>(k) * kPi / 3.0f;
        draw->AddLine(at(0.5f + 0.15f * std::cos(a), 0.5f + 0.15f * std::sin(a)),
                      at(0.5f + 0.38f * std::cos(a + 0.6f), 0.5f + 0.38f * std::sin(a + 0.6f)),
                      color, 1.0f);
    }
}

float Stroke(const Frame& at, float width = 0.07f) {
    return std::max(1.0f, at(width));
}

void Capsule(ImDrawList* draw, const Frame& at) {
    draw->AddRectFilled(at(0.26f, 0.08f), at(0.74f, 0.92f), Rgb(104, 114, 118), at(0.24f));
    draw->AddRectFilled(at(0.34f, 0.16f), at(0.5f, 0.72f), Rgb(150, 160, 164), at(0.08f));
}

void Wireframe(ImDrawList* draw, const Frame& at) {
    const ImU32 color = Rgb(150, 160, 164);
    const ImVec2 points[] = {at(0.12f, 0.8f), at(0.5f, 0.86f), at(0.88f, 0.72f),
                             at(0.3f, 0.36f), at(0.7f, 0.3f),  at(0.5f, 0.08f)};
    const int edges[][2] = {{0, 1}, {1, 2}, {0, 3}, {1, 3}, {1, 4}, {2, 4}, {3, 4}, {3, 5}, {4, 5}};
    for (const auto& edge : edges) {
        draw->AddLine(points[edge[0]], points[edge[1]], color, Stroke(at, 0.05f));
    }
    for (const ImVec2& point : points) {
        draw->AddCircleFilled(point, at(0.06f), Rgb(190, 198, 202));
    }
}

void Dots(ImDrawList* draw, const Frame& at) {
    const float spots[][2] = {{0.2f, 0.3f},  {0.45f, 0.18f}, {0.75f, 0.28f}, {0.32f, 0.55f},
                              {0.62f, 0.5f}, {0.85f, 0.62f}, {0.18f, 0.8f},  {0.5f, 0.82f}};
    for (const auto& spot : spots) {
        draw->AddCircleFilled(at(spot[0], spot[1]), at(0.07f), Rgb(150, 160, 164));
    }
}

void Scatter(ImDrawList* draw, const Frame& at) {
    for (const auto& [u, v] : {std::pair{0.0f, 0.0f}, {0.48f, 0.08f}, {0.22f, 0.48f}}) {
        Cube(draw, Frame{at(u, v), at(0.52f)});
    }
}

void SphereGlow(ImDrawList* draw, const Frame& at) {
    const ImU32 glow = Rgb(255, 236, 190);
    draw->AddCircleFilled(at(0.5f, 0.5f), at(0.24f), glow);
    for (int k = 0; k < 4; ++k) {
        const float a = static_cast<float>(k) * kPi / 2.0f + kPi / 4.0f;
        draw->AddLine(at(0.5f + 0.32f * std::cos(a), 0.5f + 0.32f * std::sin(a)),
                      at(0.5f + 0.44f * std::cos(a), 0.5f + 0.44f * std::sin(a)), glow, Stroke(at));
    }
    draw->AddCircle(at(0.5f, 0.5f), at(0.32f), IM_COL32(255, 236, 190, 115), 0, Stroke(at, 0.04f));
}

void Window(ImDrawList* draw, const Frame& at) {
    const ImU32 frame = Rgb(150, 190, 220);
    draw->AddRectFilled(at(0.14f, 0.12f), at(0.86f, 0.88f), Rgb(60, 92, 120));
    draw->AddRect(at(0.14f, 0.12f), at(0.86f, 0.88f), frame, 0.0f, Stroke(at));
    draw->AddLine(at(0.5f, 0.12f), at(0.5f, 0.88f), frame, Stroke(at, 0.05f));
    draw->AddLine(at(0.14f, 0.5f), at(0.86f, 0.5f), frame, Stroke(at, 0.05f));
}

void Lidar(ImDrawList* draw, const Frame& at) {
    const ImU32 beam = Rgb(110, 220, 180);
    for (const float v : {0.16f, 0.32f}) {
        draw->AddLine(at(0.5f, 0.42f), at(0.04f, v), beam, Stroke(at, 0.04f));
        draw->AddLine(at(0.5f, 0.42f), at(0.96f, v), beam, Stroke(at, 0.04f));
    }
    draw->AddRectFilled(at(0.28f, 0.38f), at(0.72f, 0.86f), Rgb(96, 108, 112), at(0.06f));
    draw->AddEllipseFilled(at(0.5f, 0.86f), ImVec2(at(0.22f), at(0.07f)), Rgb(96, 108, 112));
    draw->AddEllipseFilled(at(0.5f, 0.38f), ImVec2(at(0.22f), at(0.07f)), Rgb(150, 160, 164));
    draw->AddRectFilled(at(0.28f, 0.52f), at(0.72f, 0.62f), beam);
}

void RadarIcon(ImDrawList* draw, const Frame& at) {
    const ImU32 wave = Rgb(235, 150, 90);
    draw->PathArcTo(at(0.3f, 0.6f), at(0.3f), 0.25f * kPi, 1.25f * kPi, 12);
    draw->PathFillConvex(Rgb(150, 160, 164));
    draw->AddLine(at(0.3f, 0.6f), at(0.3f, 0.92f), Rgb(104, 114, 118), Stroke(at));
    for (const float radius : {0.24f, 0.4f}) {
        draw->PathArcTo(at(0.42f, 0.48f), at(radius), -0.6f * kPi, -0.1f * kPi, 8);
        draw->PathStroke(wave, Stroke(at, 0.06f));
    }
}

void Graph(ImDrawList* draw, const Frame& at, ImU32 color) {
    const ImU32 wire = Rgb(150, 160, 164);
    draw->AddBezierCubic(at(0.32f, 0.3f), at(0.55f, 0.3f), at(0.45f, 0.7f), at(0.68f, 0.7f), wire,
                         Stroke(at, 0.05f));
    draw->AddRectFilled(at(0.06f, 0.16f), at(0.4f, 0.44f), color, at(0.06f));
    draw->AddRectFilled(at(0.6f, 0.56f), at(0.94f, 0.84f), color, at(0.06f));
}

void Node(ImDrawList* draw, const Frame& at, ImU32 color) {
    draw->AddRectFilled(at(0.16f, 0.22f), at(0.84f, 0.78f), color, at(0.1f));
    draw->AddCircleFilled(at(0.16f, 0.5f), at(0.08f), Rgb(190, 198, 202));
    draw->AddCircleFilled(at(0.84f, 0.5f), at(0.08f), Rgb(190, 198, 202));
}

void Swatch(ImDrawList* draw, const Frame& at) {
    draw->AddRectFilled(at(0.12f, 0.12f), at(0.88f, 0.88f), Rgb(206, 198, 214));
    for (int row = 0; row < 3; ++row) {
        for (int column = (row % 2); column < 3; column += 2) {
            const float u = 0.12f + 0.2533f * float(column);
            const float v = 0.12f + 0.2533f * float(row);
            draw->AddRectFilled(at(u, v), at(u + 0.2533f, v + 0.2533f), Rgb(110, 92, 140));
        }
    }
}

void Rainbow(ImDrawList* draw, const Frame& at) {
    const ImU32 bands[] = {Rgb(120, 90, 200),  Rgb(80, 140, 235), Rgb(110, 200, 90),
                           Rgb(226, 220, 132), Rgb(235, 150, 90), Rgb(225, 80, 70)};
    for (int band = 0; band < 6; ++band) {
        const float u = 0.1f + 0.1333f * float(band);
        draw->AddRectFilled(at(u, 0.2f), at(u + 0.1333f, 0.8f), bands[band]);
    }
}

void Layers(ImDrawList* draw, const Frame& at) {
    const ImU32 colors[] = {Rgb(74, 85, 88), Rgb(96, 108, 112), Rgb(127, 166, 194)};
    for (int layer = 0; layer < 3; ++layer) {
        const float shift = 0.14f * float(layer);
        draw->AddRectFilled(at(0.1f + shift, 0.1f + shift), at(0.62f + shift, 0.62f + shift),
                            colors[layer], at(0.05f));
    }
}

void Gear(ImDrawList* draw, const Frame& at) {
    const ImU32 color = Rgb(127, 166, 194);
    for (int k = 0; k < 8; ++k) {
        const float a = static_cast<float>(k) * kPi / 4.0f;
        draw->AddLine(at(0.5f + 0.24f * std::cos(a), 0.5f + 0.24f * std::sin(a)),
                      at(0.5f + 0.42f * std::cos(a), 0.5f + 0.42f * std::sin(a)), color,
                      Stroke(at, 0.13f));
    }
    draw->AddCircleFilled(at(0.5f, 0.5f), at(0.28f), color);
    draw->AddCircleFilled(at(0.5f, 0.5f), at(0.11f), Rgb(46, 52, 56));
}

void Untyped(ImDrawList* draw, const Frame& at) {
    draw->AddCircle(at(0.5f, 0.5f), at(0.24f), Rgb(110, 120, 124), 0, std::max(1.2f, at(0.08f)));
}

bool IsSensorNode(std::string_view type) {
    for (uint32_t id = 0; id < SchemaCount(); ++id) {
        if (Info(SchemaId(id)).name != type) {
            continue;
        }
        for (SchemaId level = SchemaId(id); level != SchemaId::None; level = Info(level).base) {
            if (level == SchemaId::SensorNode) {
                return true;
            }
        }
        return false;
    }
    return false;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}

void ApplyTheme(const Theme& theme, float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    SetMetrics(theme, style);
    SetColors(theme, style);
    style.ScaleAllSizes(scale);
    style.FontSizeBase = theme.fontSize;
    style.FontScaleDpi = scale;
}

void DrawPrimIcon(ImDrawList& list, ImVec2 corner, float size, std::string_view type) {
    ImDrawList* draw = &list;
    Frame at{corner, size};
    if (type == "Xform") {
        Tripod(draw, at);
    } else if (type == "Scope" || type.empty()) {
        Folder(draw, at);
    } else if (type == "Sphere") {
        Ball(draw, at, Rgb(74, 85, 88), Rgb(104, 114, 118), Rgb(170, 178, 182));
    } else if (type == "Capsule") {
        Capsule(draw, at);
    } else if (type == "Cylinder" || type == "Cone") {
        Cylinder(draw, at, type == "Cone");
    } else if (type == "Cube") {
        Cube(draw, at);
    } else if (type == "Mesh") {
        Wireframe(draw, at);
    } else if (type == "Points") {
        Dots(draw, at);
    } else if (type == "PointInstancer") {
        Scatter(draw, at);
    } else if (type == "Plane" || type == "GroundPlane") {
        Plane(draw, at, type == "GroundPlane");
    } else if (type == "GeomSubset") {
        Cube(draw, at, Rgb(150, 128, 178));
    } else if (type == "Material" || EndsWith(type, "Surface")) {
        Ball(draw, at, Rgb(110, 92, 140), Rgb(150, 128, 178), Rgb(206, 198, 214));
    } else if (type == "DistantLight") {
        Sun(draw, at);
    } else if (type == "RectLight" || type == "DiskLight") {
        Emitter(draw, at, type == "DiskLight");
    } else if (type == "SphereLight") {
        SphereGlow(draw, at);
    } else if (type == "PortalLight") {
        Window(draw, at);
    } else if (type == "CylinderLight") {
        Tube(draw, at);
    } else if (type == "DomeLight") {
        Dome(draw, at);
    } else if (type == "World") {
        Globe(draw, at);
    } else if (type == "ColorBackground" || type == "GradientBackground") {
        Backdrop(draw, at, type == "GradientBackground");
    } else if (EndsWith(type, "Light")) {
        Bulb(draw, at);
    } else if (type == "Camera") {
        CameraIcon(draw, at);
    } else if (type == "Lidar") {
        Lidar(draw, at);
    } else if (type == "Radar") {
        RadarIcon(draw, at);
    } else if (type == "RenderSettings") {
        Gear(draw, at);
    } else if (type == "RenderProduct") {
        Film(draw, at);
    } else if (type == "RenderVar") {
        Layers(draw, at);
    } else if (type == "SensorPipeline") {
        Graph(draw, at, Rgb(110, 170, 150));
    } else if (type == "NodeGraph") {
        Graph(draw, at, Rgb(150, 128, 178));
    } else if (type == "UsdUVTexture") {
        Swatch(draw, at);
    } else if (type == "SampledSpectrum") {
        Rainbow(draw, at);
    } else if (IsSensorNode(type)) {
        Node(draw, at, Rgb(110, 170, 150));
    } else if (type.starts_with("Usd") || type == "ItuMaterial" || type == "TexturePlacement") {
        Node(draw, at, Rgb(150, 128, 178));
    } else if (EndsWith(type, "Filter")) {
        Aperture(draw, at);
    } else {
        Untyped(draw, at);
    }
}

}
