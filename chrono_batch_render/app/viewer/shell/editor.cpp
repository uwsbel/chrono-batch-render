// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/shell/editor.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render::app {
namespace {

std::string Leaf(const std::string& path) {
    return path.substr(path.rfind('/') + 1);
}

std::string Parent(const std::string& path) {
    return path.substr(0, path.rfind('/'));
}

bool IsShader(SchemaId schema) {
    for (SchemaId level = schema; level != SchemaId::None; level = Info(level).base) {
        if (level == SchemaId::Shader) {
            return true;
        }
    }
    return false;
}

std::vector<float> DefaultLidarElevations() {
    constexpr int kEmitters = 32;
    constexpr float kLowest = -25;
    constexpr float kHighest = 15;
    std::vector<float> elevations(kEmitters);
    for (int at = 0; at < kEmitters; ++at) {
        elevations[at] = kLowest + (kHighest - kLowest) * float(at) / (kEmitters - 1);
    }
    return elevations;
}

bool Below(const std::string& path, const std::string& ancestor) {
    return path.size() > ancestor.size() && path.starts_with(ancestor) &&
           path[ancestor.size()] == '/';
}

std::string MaterialName(SchemaId surface) {
    if (surface == SchemaId::UsdPreviewSurface) {
        return "Material";
    }
    std::string name;
    for (const char c : SurfaceLabel(surface)) {
        if (c != ' ' && c != '-') {
            name += c;
        }
    }
    return name;
}

std::optional<Prim> EnclosingMaterial(const Scene& scene, const std::string& path) {
    for (std::optional<Prim> prim = scene.Find(path); prim; prim = prim->Parent()) {
        if (prim->IsA(SchemaId::Material)) {
            return prim;
        }
    }
    return std::nullopt;
}

std::optional<Connection> Connected(const Prim& prim, const std::string& input) {
    for (const Connection& connection : prim.Connections()) {
        if (connection.input == input) {
            return connection;
        }
    }
    return std::nullopt;
}

ColumnRef BindingColumn() {
    const Span<const ColumnInfo> columns = Columns(SchemaId::MaterialBindingAPI);
    return {SchemaId::MaterialBindingAPI, &columns[0], 0};
}

bool Accepts(SchemaId schema, std::string_view input) {
    const auto named = [&](SchemaId owner) {
        return std::ranges::any_of(Columns(owner),
                                   [&](const ColumnInfo& info) { return info.field == input; });
    };
    return named(schema) ||
           (Includes(schema, SchemaId::SurfaceDetailAPI) && named(SchemaId::SurfaceDetailAPI));
}

struct Cells {
    std::string field;
    ColumnType type;
    std::vector<std::byte> bytes;
};

std::vector<Cells> ReadOwnCells(Scene& scene, const std::string& path, SchemaId schema) {
    std::vector<Cells> cells;
    const View view = scene.Select(path);
    const Span<const ColumnInfo> columns = Columns(schema);
    for (uint32_t index = 0; index < columns.size(); ++index) {
        const ColumnInfo& info = columns[index];
        if (IsArray(info.type)) {
            continue;
        }
        cells.push_back({std::string(info.field), info.type,
                         view.Get(ColumnRef{schema, &info, index}).ReadHost()});
    }
    return cells;
}

void WriteOwnCells(Scene& scene, const std::string& path, SchemaId schema,
                   const std::vector<Cells>& cells) {
    const View view = scene.Select(path);
    const Span<const ColumnInfo> columns = Columns(schema);
    for (uint32_t index = 0; index < columns.size(); ++index) {
        const ColumnInfo& info = columns[index];
        const auto found = std::ranges::find_if(cells, [&](const Cells& cell) {
            return cell.field == info.field && cell.type == info.type;
        });
        if (found != cells.end()) {
            view.Get(ColumnRef{schema, &info, index}).Write(found->bytes);
        }
    }
}

}

std::string SurfaceLabel(SchemaId surface) {
    switch (surface) {
    case SchemaId::UsdPreviewSurface:
        return "Preview Surface";
    case SchemaId::HapkeSurface:
        return "Hapke";
    case SchemaId::LommelSeeligerSurface:
        return "Lommel-Seeliger";
    case SchemaId::LunarLambertSurface:
        return "Lunar-Lambert";
    case SchemaId::OrenNayarSurface:
        return "Oren-Nayar";
    default:
        return std::string(Info(surface).name);
    }
}

std::optional<std::string> SurfaceShader(const Scene& scene, const std::string& material) {
    const std::optional<Prim> prim = scene.Find(material);
    if (!prim || !prim->IsA(SchemaId::Material)) {
        return std::nullopt;
    }
    const std::optional<Connection> surface = Connected(*prim, "surface");
    if (!surface || !scene.Find(surface->source)) {
        return std::nullopt;
    }
    return surface->source;
}

PrimGroup Classify(SchemaId schema) {
    const std::string_view name = Info(schema).name;
    if (name == "Xform" || name == "Scope" || name == "World") {
        return PrimGroup::Organize;
    }
    if (name == "PointInstancer" || name == "GeomSubset") {
        return PrimGroup::Geometry;
    }
    for (SchemaId level = schema; level != SchemaId::None; level = Info(level).base) {
        const std::string_view base = Info(level).name;
        if (base == "Gprim") {
            return PrimGroup::Geometry;
        }
        if (base.ends_with("LightBase")) {
            return PrimGroup::Lights;
        }
        if (base == "Sensor") {
            return PrimGroup::Sensors;
        }
        if (base == "SensorNode" || base == "SensorPipeline") {
            return PrimGroup::SensorPipeline;
        }
        if (base == "Connectable") {
            return PrimGroup::Shading;
        }
        if (base == "Background") {
            return PrimGroup::Backgrounds;
        }
        if (base.starts_with("Render")) {
            return PrimGroup::Render;
        }
    }
    return PrimGroup::Other;
}

std::vector<std::string> Editor::SelectedRoots() const {
    std::vector<std::string> roots;
    for (const std::string& path : m_selection.paths) {
        const bool nested = std::ranges::any_of(
            m_selection.paths, [&](const std::string& other) { return Below(path, other); });
        if (!nested && m_scene->Find(path)) {
            roots.push_back(path);
        }
    }
    return roots;
}

std::string Editor::FreeSibling(const std::string& path) const {
    for (int copy = 1;; ++copy) {
        std::string candidate = fmt::format("{}_{}", path, copy);
        if (!m_scene->Find(candidate)) {
            return candidate;
        }
    }
}

void Editor::Retarget(const std::string& from, const std::string& to) {
    for (std::string& path : m_selection.paths) {
        if (path == from) {
            path = to;
        } else if (Below(path, from)) {
            path = to + path.substr(from.size());
        }
    }
}

std::string Editor::Create(SchemaId type, const Double3& at, const Quatd& orient) {
    std::string parent;
    if (m_selection.paths.size() == 1) {
        if (const std::optional<Prim> selected = m_scene->Find(m_selection.paths.front());
            selected && (Classify(selected->Schema()) == PrimGroup::Organize ||
                         (selected->IsA(SchemaId::NodeGraph) && IsShader(type)) ||
                         (selected->IsA(SchemaId::Mesh) && type == SchemaId::GeomSubset))) {
            parent = m_selection.paths.front();
        }
    }
    const PrimGroup group = Classify(type);
    if (parent.empty() && group == PrimGroup::Shading && m_scene->Find("/Looks")) {
        parent = "/Looks";
    } else if (parent.empty() && group == PrimGroup::Render && m_scene->Find("/Render")) {
        parent = "/Render";
    }
    std::string path = parent + "/" + std::string(Info(type).name);
    if (m_scene->Find(path)) {
        path = FreeSibling(path);
    }
    const std::vector<std::string> before = m_selection.paths;
    const auto define = [this, type, path, at, orient] {
        const Prim prim = m_scene->Define(path, type);
        if (type == SchemaId::Material) {
            prim.Connect("surface", m_scene->Define(path + "/Surface", SchemaId::UsdPreviewSurface),
                         "surface");
        }
        if (type == SchemaId::Lidar) {
            prim.Set(Lidar::kEmitterElevations, DefaultLidarElevations());
        }
        if (prim.IsA(SchemaId::Xformable)) {
            prim.Set(Xformable::kTranslate, at);
            prim.Set(Xformable::kOrient, orient);
        }
        m_selection.Set(path);
    };
    m_history.Do({"Create " + Leaf(path),
                  [this, path, before] {
                      if (const std::optional<Prim> prim = m_scene->Find(path)) {
                          m_scene->Remove(*prim);
                      }
                      m_selection.paths = before;
                  },
                  define});
    return path;
}

std::string Editor::CreateMaterial(SchemaId surface) {
    const bool looks = !m_scene->Find("/Looks");
    std::string path = "/Looks/" + MaterialName(surface);
    if (m_scene->Find(path)) {
        path = FreeSibling(path);
    }

    struct Bound {
        std::string path;
        bool applied;
        std::vector<std::string> before;
    };

    auto bound = std::make_shared<std::vector<Bound>>();
    for (const std::string& selected : SelectedRoots()) {
        const std::optional<Prim> prim = m_scene->Find(selected);
        if (!prim || !prim->IsA(SchemaId::Gprim)) {
            continue;
        }
        const bool applied = prim->HasApi(SchemaId::MaterialBindingAPI);
        bound->push_back({selected, applied,
                          applied ? m_scene->Select(selected).Get(BindingColumn()).GetTargets(0)
                                  : std::vector<std::string>{}});
    }
    const std::vector<std::string> before = m_selection.paths;
    const auto define = [this, surface, path, looks, bound] {
        if (looks) {
            m_scene->Define("/Looks", SchemaId::Scope);
        }
        const Prim material = m_scene->Define(path, SchemaId::Material);
        material.Connect("surface", m_scene->Define(path + "/Surface", surface), "surface");
        for (const Bound& prim : *bound) {
            const std::optional<Prim> found = m_scene->Find(prim.path);
            if (!found) {
                continue;
            }
            if (!prim.applied) {
                m_scene->Apply(Span<const Prim>(&*found, 1), SchemaId::MaterialBindingAPI);
            }
            m_scene->Select(prim.path).Get(BindingColumn()).SetTargets(0, {path});
        }
        m_selection.Set(path);
    };
    const auto undo = [this, path, looks, bound, before] {
        for (const Bound& prim : *bound) {
            const std::optional<Prim> found = m_scene->Find(prim.path);
            if (!found) {
                continue;
            }
            if (prim.applied) {
                m_scene->Select(prim.path).Get(BindingColumn()).SetTargets(0, prim.before);
            } else {
                m_scene->Unapply(Span<const Prim>(&*found, 1), SchemaId::MaterialBindingAPI);
            }
        }
        if (const std::optional<Prim> prim = m_scene->Find(path)) {
            m_scene->Remove(*prim);
        }
        if (const std::optional<Prim> scope = m_scene->Find("/Looks");
            looks && scope && scope->Children().empty()) {
            m_scene->Remove(*scope);
        }
        m_selection.paths = before;
    };
    std::string name = fmt::format("Create {} material", SurfaceLabel(surface));
    if (!bound->empty()) {
        name += bound->size() == 1 ? " on " + Leaf(bound->front().path)
                                   : fmt::format(" on {} prims", bound->size());
    }
    m_history.Do({std::move(name), undo, define});
    return path;
}

void Editor::SetSurfaceType(const std::string& shader, SchemaId to) {
    const std::optional<Prim> prim = m_scene->Find(shader);
    if (!prim || prim->Schema() == to) {
        return;
    }
    const SchemaId from = prim->Schema();
    auto values = std::make_shared<std::vector<Cells>>(ReadOwnCells(*m_scene, shader, from));
    std::vector<Connection> dropped;
    for (const Connection& connection : prim->Connections()) {
        if (!Accepts(to, connection.input)) {
            dropped.push_back(connection);
        }
    }
    const auto retype = [this, shader](SchemaId schema, const std::vector<Cells>& cells) {
        try {
            const Prim found = *m_scene->Find(shader);
            found.Set(Shader::kId, std::string(Info(schema).name));
            WriteOwnCells(*m_scene, shader, schema, cells);
        } catch (const std::exception& error) {
            Log(LogLevel::Warning, "Changing {} to {}: {}", shader, Info(schema).name,
                error.what());
        }
    };
    const auto redo = [this, shader, to, values, dropped, retype] {
        retype(to, *values);
        if (const std::optional<Prim> found = m_scene->Find(shader)) {
            for (const Connection& connection : dropped) {
                found->Disconnect(connection.input);
            }
        }
    };
    const auto undo = [this, shader, from, values, dropped, retype] {
        retype(from, *values);
        const std::optional<Prim> found = m_scene->Find(shader);
        for (const Connection& connection : dropped) {
            if (const std::optional<Prim> source = m_scene->Find(connection.source);
                found && source) {
                found->Connect(connection.input, *source, connection.output);
            }
        }
    };
    m_history.Do({fmt::format("Change {} to {}", Leaf(shader), SurfaceLabel(to)), undo, redo});
}

void Editor::Connect(const std::string& path, const std::string& input, const std::string& source,
                     const std::string& output) {
    const std::optional<Prim> prim = m_scene->Find(path);
    if (!prim || !m_scene->Find(source)) {
        return;
    }
    const std::optional<Connection> before = Connected(*prim, input);
    const auto connect = [this, path, input](const std::optional<Connection>& connection) {
        const std::optional<Prim> found = m_scene->Find(path);
        if (!found) {
            return;
        }
        const std::optional<Prim> from =
            connection ? m_scene->Find(connection->source) : std::nullopt;
        if (from) {
            found->Connect(input, *from, connection->output);
        } else {
            found->Disconnect(input);
        }
    };
    const Connection after{input, source, output};
    m_history.Do({fmt::format("Connect {} to {}", input, Leaf(source)),
                  [connect, before] { connect(before); }, [connect, after] { connect(after); }});
}

void Editor::Bind(const std::string& path, const std::string& material) {
    const std::optional<Prim> prim = m_scene->Find(path);
    if (!prim || !m_scene->Find(material)) {
        return;
    }
    const bool applied = prim->HasApi(SchemaId::MaterialBindingAPI);
    const std::vector<std::string> before =
        applied ? m_scene->Select(path).Get(BindingColumn()).GetTargets(0)
                : std::vector<std::string>{};
    m_history.Do(
        {fmt::format("Bind {} to {}", Leaf(material), Leaf(path)),
         [this, path, applied, before] {
             const std::optional<Prim> found = m_scene->Find(path);
             if (!found) {
                 return;
             }
             if (applied) {
                 m_scene->Select(path).Get(BindingColumn()).SetTargets(0, before);
             } else {
                 m_scene->Unapply(Span<const Prim>(&*found, 1), SchemaId::MaterialBindingAPI);
             }
         },
         [this, path, material] {
             const std::optional<Prim> found = m_scene->Find(path);
             if (!found) {
                 return;
             }
             if (!found->HasApi(SchemaId::MaterialBindingAPI)) {
                 m_scene->Apply(Span<const Prim>(&*found, 1), SchemaId::MaterialBindingAPI);
             }
             m_scene->Select(path).Get(BindingColumn()).SetTargets(0, {material});
         }});
}

void Editor::Disconnect(const std::string& path, const std::string& input) {
    const std::optional<Prim> prim = m_scene->Find(path);
    if (!prim) {
        return;
    }
    const std::optional<Connection> before = Connected(*prim, input);
    if (!before) {
        return;
    }
    m_history.Do({fmt::format("Disconnect {}", input),
                  [this, path, before] {
                      const std::optional<Prim> found = m_scene->Find(path);
                      const std::optional<Prim> source = m_scene->Find(before->source);
                      if (found && source) {
                          found->Connect(before->input, *source, before->output);
                      }
                  },
                  [this, path, input] {
                      if (const std::optional<Prim> found = m_scene->Find(path)) {
                          found->Disconnect(input);
                      }
                  }});
}

void Editor::ConnectNode(const std::string& path, const std::string& input, SchemaId node,
                         const std::string& output, const std::string& file) {
    const std::optional<Prim> prim = m_scene->Find(path);
    if (!prim) {
        return;
    }
    const std::optional<Prim> material = EnclosingMaterial(*m_scene, path);
    const std::string parent = material ? material->Path() : Parent(path);
    const bool texture = node == SchemaId::UsdUVTexture;
    const std::string leaf = texture                              ? input + "Texture"
                             : node == SchemaId::SampledSpectrum  ? input + "Spectrum"
                             : node == SchemaId::TexturePlacement ? std::string("Placement")
                                                                  : std::string(Info(node).name);
    std::string created = fmt::format("{}/{}", parent, leaf);
    if (m_scene->Find(created)) {
        created = FreeSibling(created);
    }
    std::string placement;
    bool placed = false;
    if (texture) {
        for (const Prim& child : m_scene->Find(parent)->Children()) {
            if (child.IsA(SchemaId::TexturePlacement)) {
                placement = child.Path();
            }
        }
        if (placement.empty()) {
            placement = parent + "/Placement";
            placed = !m_scene->Find(placement);
            if (!placed) {
                placement = FreeSibling(placement);
                placed = true;
            }
        }
    }
    const std::optional<Connection> before = Connected(*prim, input);
    const auto redo = [this, path, input, node, output, file, created, placement, placed] {
        const Prim made = m_scene->Define(created, node);
        if (!file.empty()) {
            made.Set(UsdUVTexture::kFile, file);
        }
        if (!placement.empty()) {
            const Prim at = placed ? m_scene->Define(placement, SchemaId::TexturePlacement)
                                   : *m_scene->Find(placement);
            made.Connect("st", at, "st");
        }
        m_scene->Find(path)->Connect(input, made, output);
    };
    const auto undo = [this, path, input, created, placement, placed, before] {
        const std::optional<Prim> found = m_scene->Find(path);
        const std::optional<Prim> source = before ? m_scene->Find(before->source) : std::nullopt;
        if (found && source) {
            found->Connect(input, *source, before->output);
        } else if (found) {
            found->Disconnect(input);
        }
        if (const std::optional<Prim> made = m_scene->Find(created)) {
            m_scene->Remove(*made);
        }
        if (const std::optional<Prim> at = m_scene->Find(placement); placed && at) {
            m_scene->Remove(*at);
        }
    };
    m_history.Do({fmt::format("Connect {} to a new {}", input, Info(node).name), undo, redo});
}

void Editor::ApplyApi(SchemaId api, const std::string& instance) {
    std::vector<std::string> paths;
    for (const std::string& path : m_selection.paths) {
        const std::optional<Prim> prim = m_scene->Find(path);
        if (!prim || !AppliesTo(api, prim->Schema())) {
            continue;
        }
        const std::vector<std::string> instances = prim->Instances(api);
        const bool applied = instance.empty()
                                 ? prim->HasApi(api)
                                 : std::ranges::find(instances, instance) != instances.end();
        if (!applied) {
            paths.push_back(path);
        }
    }
    if (paths.empty()) {
        return;
    }
    const auto run = [scene = m_scene, api, instance, paths](bool apply) {
        std::vector<Prim> prims;
        for (const std::string& path : paths) {
            if (const std::optional<Prim> prim = scene->Find(path)) {
                prims.push_back(*prim);
            }
        }
        try {
            if (apply && instance.empty()) {
                scene->Apply(prims, api);
            } else if (apply) {
                scene->Apply(prims, api, instance);
            } else if (instance.empty()) {
                scene->Unapply(prims, api);
            } else {
                scene->Unapply(prims, api, instance);
            }
        } catch (const std::exception& error) {
            Log(LogLevel::Warning, "{} {}: {}", apply ? "Apply" : "Unapply", Info(api).name,
                error.what());
        }
    };
    std::string name = fmt::format("Apply {}", Info(api).name);
    if (!instance.empty()) {
        name += ":" + instance;
    }
    m_history.Do({std::move(name), [run] { run(false); }, [run] { run(true); }});
}

void Editor::SetVisible(const std::string& path, bool visible) {
    const auto set = [scene = m_scene, path](bool shown) {
        scene->Select(path)
            .Get(Imageable::kVisibility)
            .Set(shown ? Visibility::Inherited : Visibility::Invisible);
    };
    m_history.Do({fmt::format("{} {}", visible ? "Show" : "Hide", Leaf(path)),
                  [set, visible] { set(!visible); }, [set, visible] { set(visible); }});
}

void Editor::SetCells(std::string name, Target target, ColumnRef column,
                      std::vector<std::byte> before, std::vector<std::byte> after) {
    const auto write = [target, column, name](const std::vector<std::byte>& cells, bool fill) {
        try {
            UntypedAttribute attribute = target().Get(column);
            if (fill) {
                attribute.Fill(cells);
            } else {
                attribute.Write(cells);
            }
        } catch (const std::exception& error) {
            Log(LogLevel::Warning, "{}: {}", name, error.what());
        }
    };
    m_history.Do(
        {name, [write, before] { write(before, false); }, [write, after] { write(after, true); }});
}

void Editor::SetPose(std::string name, const std::string& path, Double3 translateBefore,
                     Quatd orientBefore, Float3 scaleBefore, Double3 translateAfter,
                     Quatd orientAfter, Float3 scaleAfter) {
    const auto write = [scene = m_scene, path, name](Double3 translate, Quatd orient,
                                                     Float3 scale) {
        try {
            const View prim = scene->Select(path);
            prim.Get(Xformable::kTranslate).Set(translate);
            prim.Get(Xformable::kOrient).Set(orient);
            prim.Get(Xformable::kScale).Set(scale);
        } catch (const std::exception& error) {
            Log(LogLevel::Warning, "{}: {}", name, error.what());
        }
    };
    m_history.Push({std::move(name),
                    [write, translateBefore, orientBefore, scaleBefore] {
                        write(translateBefore, orientBefore, scaleBefore);
                    },
                    [write, translateAfter, orientAfter, scaleAfter] {
                        write(translateAfter, orientAfter, scaleAfter);
                    }});
}

void Editor::SetTargets(std::string name, Target target, ColumnRef column,
                        std::vector<std::vector<std::string>> before,
                        std::vector<std::string> after) {
    const auto undo = [target, column, name, before] {
        try {
            UntypedAttribute attribute = target().Get(column);
            for (uint32_t row = 0; row < before.size(); ++row) {
                attribute.SetTargets(row, before[row]);
            }
        } catch (const std::exception& error) {
            Log(LogLevel::Warning, "{}: {}", name, error.what());
        }
    };
    const auto redo = [target, column, name, after, rows = uint32_t(before.size())] {
        try {
            UntypedAttribute attribute = target().Get(column);
            for (uint32_t row = 0; row < rows; ++row) {
                attribute.SetTargets(row, after);
            }
        } catch (const std::exception& error) {
            Log(LogLevel::Warning, "{}: {}", name, error.what());
        }
    };
    m_history.Do({std::move(name), undo, redo});
}

bool Editor::Rename(const std::string& path, std::string_view name) {
    const std::string to = Parent(path) + "/" + std::string(name);
    if (to == path) {
        return true;
    }
    try {
        m_scene->Rename(*m_scene->Find(path), name);
    } catch (const std::exception& error) {
        Log(LogLevel::Warning, "Renaming {} to {}: {}", path, name, error.what());
        return false;
    }
    Retarget(path, to);
    const std::string old = Leaf(path);
    m_history.Push({fmt::format("Rename {} to {}", old, name),
                    [this, to, old, path] {
                        m_scene->Rename(*m_scene->Find(to), old);
                        Retarget(to, path);
                    },
                    [this, to, path, name = std::string(name)] {
                        m_scene->Rename(*m_scene->Find(path), name);
                        Retarget(path, to);
                    }});
    return true;
}

bool Editor::Reparent(const std::string& path, const std::string& parent) {
    const std::string to = parent + "/" + Leaf(path);
    if (to == path || parent == path || Below(parent, path)) {
        return false;
    }
    try {
        m_scene->Move(*m_scene->Find(path), to);
    } catch (const std::exception& error) {
        Log(LogLevel::Warning, "Moving {} under {}: {}", path, parent.empty() ? "/" : parent,
            error.what());
        return false;
    }
    Retarget(path, to);
    m_history.Push({fmt::format("Move {}", Leaf(path)),
                    [this, to, path] {
                        m_scene->Move(*m_scene->Find(to), path);
                        Retarget(to, path);
                    },
                    [this, to, path] {
                        m_scene->Move(*m_scene->Find(path), to);
                        Retarget(path, to);
                    }});
    return true;
}

void Editor::Duplicate() {
    struct Pasted {
        PrimCopy copy;
        std::string path;
    };

    auto pasted = std::make_shared<std::vector<Pasted>>();
    for (const std::string& path : SelectedRoots()) {
        pasted->push_back({m_scene->Copy(*m_scene->Find(path)), FreeSibling(path)});
    }
    if (pasted->empty()) {
        return;
    }
    const auto paste = [this, pasted] {
        m_selection.Clear();
        for (const Pasted& item : *pasted) {
            m_scene->Paste(item.copy, item.path);
            m_selection.paths.push_back(item.path);
        }
    };
    const std::vector<std::string> before = m_selection.paths;
    m_history.Do({pasted->size() == 1 ? "Duplicate " + Leaf(pasted->front().path)
                                      : fmt::format("Duplicate {} prims", pasted->size()),
                  [this, pasted, before] {
                      for (const Pasted& item : *pasted) {
                          if (const std::optional<Prim> prim = m_scene->Find(item.path)) {
                              m_scene->Remove(*prim);
                          }
                      }
                      m_selection.paths = before;
                  },
                  paste});
}

void Editor::Delete() {
    struct Removed {
        PrimCopy copy;
        std::string path;
    };

    auto removed = std::make_shared<std::vector<Removed>>();
    for (const std::string& path : SelectedRoots()) {
        removed->push_back({m_scene->Copy(*m_scene->Find(path)), path});
    }
    if (removed->empty()) {
        return;
    }
    const std::vector<std::string> before = m_selection.paths;
    const auto remove = [this, removed] {
        for (const Removed& item : *removed) {
            if (const std::optional<Prim> prim = m_scene->Find(item.path)) {
                m_scene->Remove(*prim);
            }
        }
        m_selection.Clear();
    };
    m_history.Do({removed->size() == 1 ? "Delete " + Leaf(removed->front().path)
                                       : fmt::format("Delete {} prims", removed->size()),
                  [this, removed, before] {
                      for (const Removed& item : *removed) {
                          m_scene->Paste(item.copy, item.path);
                      }
                      m_selection.paths = before;
                  },
                  remove});
}

}
