// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>

#include "chrono_batch_render/app/viewer/panels/panels.h"
#include "chrono_batch_render/viewer_xr_export.h"

extern "C" CHRONO_BATCH_RENDER_VIEWER_XR_API void
ChronoBatchRenderAddXrPanel(chrono::batch_render::app::Viewer* viewer,
                            const chrono::batch_render::Ref<chrono::batch_render::Scene>* scene,
                            std::shared_ptr<chrono::batch_render::app::XrPanel>* panel);
