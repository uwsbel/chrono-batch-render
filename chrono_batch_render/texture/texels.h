// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "chrono_batch_render/util/span.h"

namespace chrono::batch_render {

inline constexpr std::array<char, 8> kTexelMagic = {'C', 'B', 'R', 'T', 'E', 'X', 0, 1};

struct TexelHeader {
    std::array<char, 8> magic = kTexelMagic;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    uint32_t floats = 0;
};

static_assert(sizeof(TexelHeader) == 24);

inline std::optional<TexelHeader> ReadTexelHeader(Span<const std::byte> bytes) {
    TexelHeader header;
    if (bytes.size() < sizeof(header)) {
        return std::nullopt;
    }
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kTexelMagic) {
        return std::nullopt;
    }
    return header;
}

}
