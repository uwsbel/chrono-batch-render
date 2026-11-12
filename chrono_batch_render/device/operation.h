// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>

#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/export.h"
#include "chrono_batch_render/util/span.h"

namespace chrono::batch_render::device {

class Buffer;
class Device;
class Queue;

struct CHRONO_BATCH_RENDER_API Operation {
    Queue* queue = nullptr;
    uint64_t value = 0;

    bool Ready() const;

    void Wait() const;

    void Wait(Stream stream) const;
};

class CHRONO_BATCH_RENDER_API Download : public Operation {
  public:
    Download();
    ~Download();
    Download(const Download& other);
    Download(Download&& other) noexcept;
    Download& operator=(const Download& other);
    Download& operator=(Download&& other) noexcept;

    Span<const std::byte> Bytes() const;

  private:
    friend class Queue;

    Ref<Device> m_device;
    Ref<Buffer> m_staging;
    uint64_t m_offset = 0;
    uint64_t m_bytes = 0;
};

}
