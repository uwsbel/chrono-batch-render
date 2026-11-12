// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>
#include <string_view>

#include "chrono_batch_render/io/image.h"
#include "chrono_batch_render/io/kernels/ies.h"

namespace chrono::batch_render {

bool IsIes(const std::filesystem::path& path);

Image ReadIes(std::string_view text);

}
