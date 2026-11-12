// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

static const uint kNoPrim = 0xFFFFFFFF;
static const uint kNoRow = 0xFFFFFFFF;
static const uint kGlobalWorld = 0xFFFFFFFF;

CHRONO_BATCH_RENDER_SHARED uint WorldSlot(uint world) {
    return world == kGlobalWorld ? 0 : world + 1;
}

CHRONO_BATCH_RENDER_SHARED uint SlotWorld(uint slot) {
    return slot == 0 ? kGlobalWorld : slot - 1;
}

CHRONO_BATCH_RENDER_SHARED uint TlasIndex(uint world, uint worldCount) {
    return world < worldCount ? world : worldCount;
}

CHRONO_BATCH_RENDER_SHARED uint TlasWorld(uint tlas, uint worldCount) {
    return tlas < worldCount ? tlas : kGlobalWorld;
}

}
