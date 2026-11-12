// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "chrono_batch_render/app/viewer/shell/capture.h"
#include "chrono_batch_render/app/viewer/shell/editor.h"
#include "chrono_batch_render/app/viewer/shell/selection.h"
#include "chrono_batch_render/app/viewer/theme/theme.h"
#include "chrono_batch_render/app/viewer/viewer.h"
#include "chrono_batch_render/app/viewer/widgets/file_dialog.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/scene/scene.h"

namespace chrono::batch_render::app {

struct Panel {
    std::string title;
    Dock dock;
    std::function<void()> draw;
    bool visible = true;
    std::function<void()> hidden;
    bool builtin = false;
};

Panel CreateInfoPanel(Ref<Scene> scene, Ref<device::Device> device,
                      std::shared_ptr<RenderDelegate> delegate);

Panel CreateLogPanel(const Theme& theme);

Panel CreateCapturePanel(Capture& capture, FileDialog& files);

Panel CreateProfilerPanel();

Panel CreateMemoryPanel(Ref<device::Device> device);

Panel CreateStagePanel(Ref<Scene> scene, Selection& selection, Editor& editor, const Theme& theme,
                       std::function<void()> drawCreate);

Panel CreatePropertiesPanel(Ref<Scene> scene, Selection& selection, Editor& editor,
                            FileDialog& dialog, const Theme& theme);

class XrPanel {
  public:
    virtual ~XrPanel() = default;
    virtual void Start() = 0;
    virtual void Simulate(bool simulated) = 0;
    virtual void Spectate(bool spectating) = 0;
    virtual void Replay(std::filesystem::path recording) = 0;
    virtual void Record(std::filesystem::path file) = 0;
    virtual void Stop() = 0;
    virtual bool Running() const = 0;
    virtual uint64_t FramesPresented() const = 0;
};

std::shared_ptr<XrPanel> AddXrPanel(Viewer& viewer, Ref<Scene> scene);

using AddXrPanelEntry = void (*)(Viewer* viewer, const Ref<Scene>* scene,
                                 std::shared_ptr<XrPanel>* panel);

inline constexpr const char* kAddXrPanel = "ChronoBatchRenderAddXrPanel";

std::shared_ptr<XrPanel> LoadXrPanel(Viewer& viewer, const Ref<Scene>& scene);

void DrawControls(bool& open, const Theme& theme);

void DrawAbout(bool& open, device::Device& device);

}
