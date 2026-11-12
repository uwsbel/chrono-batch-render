// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/widgets/widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace chrono::batch_render::app {

std::string FormatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? fmt::format("{:.0f} {}", value, units[unit])
                     : fmt::format("{:.1f} {}", value, units[unit]);
}

std::string Lowercase(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower;
}

void Row(const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(9.0f * ImGui::GetFontSize());
    ImGui::TextUnformatted(value.c_str());
}

bool RowButton(const char* icon, const char* tip) {
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    const float size = high.y - low.y;
    const ImVec2 after = ImGui::GetCursorScreenPos();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 extent = window->DC.CursorMaxPos;
    ImGui::SetCursorScreenPos({high.x - size, low.y});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool clicked = ImGui::Button(icon, {size, size});
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tip);
    window->DC.CursorMaxPos = extent;
    window->DC.CursorPos = after;
    window->DC.IsSetPos = false;
    if (clicked) {
        ImGui::CloseCurrentPopup();
    }
    return clicked;
}

bool IconButton(const char* id, const char* icon, const char* tip, bool flat) {
    const float size = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    if (flat) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    }
    const bool clicked = ImGui::Button("##icon", {size, size});
    if (flat) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
    ImGui::SetItemTooltip("%s", tip);
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 centre{low.x + 0.5f * size, low.y + 0.5f * size};
    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, icon, nullptr);
    ImFontBaked* baked = ImGui::GetFontBaked();
    const ImFontGlyph* glyph =
        baked != nullptr ? baked->FindGlyphNoFallback(ImWchar(codepoint)) : nullptr;
    ImVec2 at = {centre.x - 0.5f * ImGui::CalcTextSize(icon).x,
                 centre.y - 0.5f * ImGui::GetFontSize()};
    if (glyph != nullptr) {
        at = {centre.x - 0.5f * (glyph->X0 + glyph->X1), centre.y - 0.5f * (glyph->Y0 + glyph->Y1)};
    }
    const ImGuiCol ink = flat && !ImGui::IsItemHovered() ? ImGuiCol_TextDisabled : ImGuiCol_Text;
    ImGui::GetWindowDrawList()->AddText({std::round(at.x), std::round(at.y)},
                                        ImGui::GetColorU32(ink), icon);
    return clicked;
}

}
