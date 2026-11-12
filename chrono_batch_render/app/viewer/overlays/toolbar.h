// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "chrono_batch_render/app/viewer/shell/editor.h"
#include "chrono_batch_render/app/viewer/theme/theme.h"
#include "chrono_batch_render/app/viewer/viewport/viewport.h"
#include "chrono_batch_render/scene/scene.h"

namespace chrono::batch_render::app {

class Toolbar {
  public:
    Toolbar(Ref<Scene> scene, std::string product, Viewport& viewport, const Theme& theme,
            Editor* editor);

    void Draw(ImVec2 min, float right);

  private:
    void Refresh();
    bool BeginPill(const char* id, const std::string& label, const char* tip);
    void DrawSettingsMenu();
    void DrawRendererMenu();
    void DrawCameraMenu();
    void DrawAovMenu();
    void DrawLightingMenu();
    void DrawHudMenu();
    void DrawWorldsMenu();
    void RefreshLens(const std::string& camera);
    void DrawLensToggle();
    void DrawLensRow(float left, float right);
    void DrawLensField(size_t field, const char* label, const char* tip, float scale, float speed,
                       float low, float high, const char* format);

    Ref<Scene> m_scene;
    std::string m_product;
    Viewport& m_viewport;
    const Theme& m_theme;
    uint64_t m_structure = ~uint64_t(0);
    std::vector<std::string> m_cameras;
    std::vector<device::Download> m_reads;
    double m_requested = -1;
    RenderMode m_mode = RenderMode::RealTime;
    MinimalMode m_minimal = MinimalMode::Full;
    std::string m_worldSearch;
    Editor* m_editor = nullptr;
    bool m_lensOpen = false;
    float m_lensLeft = 0;
    float m_lensRight = 0;
    bool m_lensValid = false;
    bool m_orthographic = false;
    std::string m_lensCamera;
    std::vector<device::Download> m_lensReads;
    double m_lensRequested = -1;
    std::array<float, 8> m_lens{};
    std::optional<float> m_lensBefore;
};

}
