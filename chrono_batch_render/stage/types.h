// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/export.h"
#include "chrono_batch_render/stage/schema/ids.h"
#include "chrono_batch_render/stage/schema/types.h"

namespace chrono::batch_render {

struct ComposedWorldDesc {
    std::vector<uint32_t> worlds;
    std::vector<Double3> offsets;
    bool global = true;
    bool stageLights = true;
};

struct IdLabel {
    std::string path;
    std::map<std::string, std::vector<std::string>> labels;
};

using IdMap = std::map<std::array<uint32_t, 4>, IdLabel>;
using IdTexts = std::map<std::array<uint32_t, 4>, std::string>;

using Ordinal = uint64_t;
using FileBytes = std::shared_ptr<const std::vector<std::byte>>;

struct CHRONO_BATCH_RENDER_API CloneDesc {
    bool moveSource = false;
    bool attachable = false;
};

struct CHRONO_BATCH_RENDER_API BatchDesc {
    bool attachable = false;
};

struct CHRONO_BATCH_RENDER_API PrimRange {
    uint32_t firstPrim = kNoPrim;
    uint32_t count = 0;
    uint32_t copies = 0;
    uint32_t firstWorld = kGlobalWorld;
    uint32_t worldsPerCopy = 0;

    uint32_t Index(uint32_t copy, uint32_t index) const {
        return firstPrim + copy * count + index;
    }

    uint32_t Size() const {
        return count * copies;
    }
};

struct CHRONO_BATCH_RENDER_API Box {
    Float3 lower;
    Float3 upper;
};

struct CHRONO_BATCH_RENDER_API Snapshot {
    Ordinal ordinal = 0;
    device::Operation ready;
    uint64_t tables = 0;
    std::shared_ptr<const void> pinned;

    uint64_t Version() const;
    uint64_t Version(std::string_view table) const;
};

}
