// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "chrono_batch_render/stage/columns/kernels/authored.h"
#include "chrono_batch_render/stage/publish/table.h"

namespace chrono::batch_render {

class SnapshotHeader {
  public:
    virtual ~SnapshotHeader() = default;

    virtual void Describe(const Table& table) = 0;
    virtual std::span<const std::byte> Seal(const AuthoredData& data, uint32_t worldCount) = 0;
};

}
