// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <optional>

#include "chrono_batch_render/export.h"

namespace chrono::batch_render {

CHRONO_BATCH_RENDER_API std::optional<uint64_t> ResidentBytes();

}
