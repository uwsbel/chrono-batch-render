// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "chrono_batch_render/bvh/kernels/bvh.h"
#include "chrono_batch_render/device/accel.h"
#include "chrono_batch_render/device/adapter.h"
#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/export.h"

namespace chrono::batch_render::device {
class Device;
}

namespace chrono::batch_render::bvh {

using Geometry = std::variant<device::AccelTriangles, device::AccelBoxes>;

class CHRONO_BATCH_RENDER_API Blas : public RefCounted {
  public:
    device::Traversal Format() const {
        return m_format;
    }

    virtual uint64_t Handle() const = 0;

  protected:
    explicit Blas(device::Traversal format) : m_format(format) {}

    ~Blas() override = default;

  private:
    device::Traversal m_format;
};

class CHRONO_BATCH_RENDER_API Tlas : public RefCounted {
  public:
    device::Traversal Format() const {
        return m_format;
    }

    uint32_t Count() const {
        return uint32_t(m_capacities.size());
    }

    std::span<const uint32_t> Capacities() const {
        return m_capacities;
    }

    uint64_t Handle(uint32_t world = 0) const;

    device::Buffer& Handles() const {
        return *m_table;
    }

  protected:
    Tlas(device::Traversal format, std::span<const uint32_t> capacities);
    ~Tlas() override = default;

    void SetHandles(device::Device& device, std::vector<uint64_t> handles);

  private:
    device::Traversal m_format;
    std::vector<uint32_t> m_capacities;
    std::vector<uint64_t> m_handles;
    Ref<device::Buffer> m_table;
};

class CHRONO_BATCH_RENDER_API Builder {
  public:
    static std::unique_ptr<Builder> Create(device::Device& device,
                                           device::Traversal format = device::Traversal::Auto);

    virtual ~Builder() = default;
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    virtual device::Traversal Format() const = 0;

    virtual void Prepare() {}

    virtual Ref<Blas> CreateBlas(const Geometry& geometry) = 0;
    virtual Ref<Tlas> CreateTlas(std::span<const uint32_t> capacities) = 0;

    virtual void Build(device::CommandEncoder& encoder, std::span<const Ref<Blas>> blases) = 0;
    virtual void Refit(device::CommandEncoder& encoder, std::span<const Ref<Blas>> blases) = 0;
    virtual void Build(device::CommandEncoder& encoder, Tlas& tlas, device::BufferRange instances,
                       std::optional<device::BufferRange> counts = std::nullopt,
                       std::span<const uint32_t> worlds = {}) = 0;
    virtual void Refit(device::CommandEncoder& encoder, Tlas& tlas, device::BufferRange instances,
                       std::optional<device::BufferRange> counts = std::nullopt,
                       std::span<const uint32_t> worlds = {}) = 0;
    virtual void SubmitRefit(device::CommandEncoder&& encoder, Tlas& tlas,
                             device::BufferRange instances,
                             std::optional<device::BufferRange> counts = std::nullopt,
                             std::span<const uint32_t> worlds = {});

  protected:
    explicit Builder(device::Device& device) : m_device(device) {}

    device::Device& m_device;
};

}
