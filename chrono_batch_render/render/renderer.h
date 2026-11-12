// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/export.h"
#include "chrono_batch_render/overlay/overlay.h"
#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/util/span.h"

namespace chrono::batch_render {

class RendererImpl;
class Scene;

enum class Streaming : uint8_t {
    Blocking,
    Progressive,
};

struct CHRONO_BATCH_RENDER_API RendererDesc {
    uint64_t maxPixelsPerLaunch = uint64_t(1) << 30;
    Ref<Overlays> overlays;
    Streaming streaming = Streaming::Blocking;
};

class CHRONO_BATCH_RENDER_API Renderer {
  public:
    explicit Renderer(Ref<Scene> scene, const RendererDesc& desc = {});
    ~Renderer();
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;

    RenderOutput Step(Span<const std::string> products, Stream stream = Stream::None());
    RenderOutput Step(std::string_view product, Stream stream = Stream::None());

    const Ref<Overlays>& GetOverlays() const;

  private:
    std::unique_ptr<RendererImpl> m_impl;
};

}
