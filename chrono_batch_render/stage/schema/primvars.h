// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "chrono_batch_render/device/kernels/shared.h"

namespace chrono::batch_render {

CHRONO_BATCH_RENDER_ENUM(PrimvarType) {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Int2,
    Int3,
    Int4,
};

struct PrimvarEntry {
    uint name;
    uint type;
    uint interpolation;
    uint elementSize;
    uint values;
    uint valueCount;
    uint indices;
    uint indexCount;
};

CHRONO_BATCH_RENDER_CHECK_SIZE(PrimvarEntry, 32);

static const uint kPrimvarEntryWords = 8;

CHRONO_BATCH_RENDER_SHARED uint PrimvarComponents(PrimvarType type) {
    return uint(type) % 4 + 1;
}

CHRONO_BATCH_RENDER_SHARED bool PrimvarIsInt(PrimvarType type) {
    return uint(type) >= uint(PrimvarType::Int);
}

#ifdef __SLANG__

bool FindPrimvarEntry(uint64_t table, uint words, uint name, out PrimvarEntry found) {
    found = {};
    if (words == 0) {
        return false;
    }
    const Ptr<PrimvarEntry> entries = Ptr<PrimvarEntry>(table + sizeof(uint));
    uint low = 0;
    uint high = Ptr<uint>(table)[0];
    while (low < high) {
        const uint middle = (low + high) / 2;
        const PrimvarEntry entry = entries[middle];
        if (entry.name == name) {
            found = entry;
            return true;
        }
        if (entry.name < name) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return false;
}

#endif

}
