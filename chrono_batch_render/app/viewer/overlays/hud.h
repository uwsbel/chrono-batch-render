// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "chrono_batch_render/app/viewer/theme/theme.h"

namespace chrono::batch_render::app {

class Hud {
  public:
    float Draw(ImDrawList& list, ImVec2 topRight, const Theme& theme,
               const std::function<std::vector<std::string>()>& lines);

  private:
    double m_refreshed = -1;
    std::vector<std::string> m_lines;
};

}
