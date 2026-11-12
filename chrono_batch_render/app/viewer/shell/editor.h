// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chrono_batch_render/app/viewer/shell/history.h"
#include "chrono_batch_render/app/viewer/shell/selection.h"
#include "chrono_batch_render/scene/scene.h"

namespace chrono::batch_render::app {

enum class PrimGroup {
    Organize,
    Geometry,
    Lights,
    Sensors,
    Shading,
    SensorPipeline,
    Backgrounds,
    Render,
    Other
};

inline constexpr const char* kPrimGroupNames[] = {"Organize",    "Geometry",      "Lights",
                                                  "Sensors",     "Shading Nodes", "Sensor Pipeline",
                                                  "Backgrounds", "Rendering",     "Other"};

PrimGroup Classify(SchemaId schema);

std::string SurfaceLabel(SchemaId surface);

std::optional<std::string> SurfaceShader(const Scene& scene, const std::string& material);

class Editor {
  public:
    using Target = std::function<View()>;

    Editor(Ref<Scene> scene, Selection& selection, History& history)
        : m_scene(std::move(scene)), m_selection(selection), m_history(history) {}

    std::string Create(SchemaId type, const Double3& at, const Quatd& orient);
    std::string CreateMaterial(SchemaId surface);
    void SetSurfaceType(const std::string& shader, SchemaId to);
    void Connect(const std::string& path, const std::string& input, const std::string& source,
                 const std::string& output);
    void Disconnect(const std::string& path, const std::string& input);
    void Bind(const std::string& path, const std::string& material);
    void ConnectNode(const std::string& path, const std::string& input, SchemaId node,
                     const std::string& output, const std::string& file);
    void SetVisible(const std::string& path, bool visible);
    void ApplyApi(SchemaId api, const std::string& instance = {});
    void SetCells(std::string name, Target target, ColumnRef column, std::vector<std::byte> before,
                  std::vector<std::byte> after);
    void SetTargets(std::string name, Target target, ColumnRef column,
                    std::vector<std::vector<std::string>> before, std::vector<std::string> after);
    void SetPose(std::string name, const std::string& path, Double3 translateBefore,
                 Quatd orientBefore, Float3 scaleBefore, Double3 translateAfter, Quatd orientAfter,
                 Float3 scaleAfter);
    bool Rename(const std::string& path, std::string_view name);
    bool Reparent(const std::string& path, const std::string& parent);
    void Duplicate();
    void Delete();

  private:
    std::vector<std::string> SelectedRoots() const;
    std::string FreeSibling(const std::string& path) const;
    void Retarget(const std::string& from, const std::string& to);

    Ref<Scene> m_scene;
    Selection& m_selection;
    History& m_history;
};

}
