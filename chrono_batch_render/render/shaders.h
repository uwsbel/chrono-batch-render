// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "chrono_batch_render/export.h"
#include "chrono_batch_render/scene/scene.h"

namespace chrono::batch_render {

CHRONO_BATCH_RENDER_API std::vector<std::string> LoadShaders(Scene& scene,
                                                             const std::filesystem::path& file);

CHRONO_BATCH_RENDER_API std::vector<std::string> ReloadShaders(Scene& scene);

CHRONO_BATCH_RENDER_API std::vector<std::filesystem::path> LoadedShaderFiles();

CHRONO_BATCH_RENDER_API std::vector<SchemaId> SurfaceShaders();

}
