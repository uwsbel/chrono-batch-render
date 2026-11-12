// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>

#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/kernels/shared.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/util/error.h"

namespace chrono::batch_render::device {

template <class T>
Ptr<T> RequireRange(const Program& program, const char* what, BufferRange range, uint64_t count) {
    program.GetDevice().Require(range.GetBuffer(), what);
    if (range.Bytes() < count * sizeof(T)) {
        throw Error("{} needs {} bytes of buffer \"{}\"; the range it was given has {}", what,
                    count * sizeof(T), range.GetBuffer().Label(), range.Bytes());
    }
    return {range.Address()};
}

}
