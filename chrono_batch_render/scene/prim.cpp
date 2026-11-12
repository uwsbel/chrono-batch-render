// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/scene/prim.h"

#include <algorithm>

#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/scene_impl.h"
#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render {

Scene& Prim::GetScene() const {
    if (m_scene == nullptr) {
        throw Error("An empty prim belongs to no scene");
    }
    return *m_scene;
}

bool Prim::Valid() const {
    const auto lock = LockScene(m_scene);
    if (m_scene == nullptr) {
        return false;
    }
    const Prims& prims = SceneImpl::Get(*m_scene).GetStructure().GetPrims();
    return prims.Alive(m_index) && prims.Generation(m_index) == m_generation;
}

void Prim::Check() const {
    const auto lock = LockScene(m_scene);
    if (!Valid()) {
        throw Error("Prim {} was removed", m_index);
    }
}

SchemaId Prim::Schema() const {
    const auto lock = LockScene(m_scene);
    Check();
    return SceneImpl::Get(*m_scene).GetStructure().GetPrims().Record(m_index).schema;
}

bool Prim::IsSession() const {
    const auto lock = LockScene(m_scene);
    Check();
    return SceneImpl::Get(*m_scene).GetStructure().GetPrims().Session(m_index);
}

uint32_t Prim::World() const {
    const auto lock = LockScene(m_scene);
    Check();
    return SceneImpl::Get(*m_scene).GetStructure().GetPrims().Record(m_index).world;
}

std::string Prim::Path() const {
    const auto lock = LockScene(m_scene);
    Check();
    return SceneImpl::Get(*m_scene).GetStructure().GetPaths().Path(m_index);
}

std::string_view Prim::Name() const {
    const auto lock = LockScene(m_scene);
    Check();
    return SceneImpl::Get(*m_scene).GetStructure().GetPaths().Name(m_index);
}

std::optional<Prim> Prim::Parent() const {
    const auto lock = LockScene(m_scene);
    Check();
    const uint32_t parent =
        SceneImpl::Get(*m_scene).GetStructure().GetPrims().Record(m_index).parent;
    if (parent == kNoPrim) {
        return std::nullopt;
    }
    return SceneImpl::Get(*m_scene).Handle(parent);
}

std::vector<Prim> Prim::Children() const {
    const auto lock = LockScene(m_scene);
    Check();
    std::vector<Prim> children;
    for (const uint32_t child :
         SceneImpl::Get(*m_scene).GetStructure().GetPaths().Children(m_index)) {
        children.push_back(SceneImpl::Get(*m_scene).Handle(child));
    }
    return children;
}

std::vector<Connection> Prim::Connections() const {
    const auto lock = LockScene(m_scene);
    CheckSchema(SchemaId::Connectable);
    const std::vector<std::string> sources = Get(Connectable::kConnectionsSources);
    const std::vector<std::string> inputs = Get(Connectable::kConnectionsInputs);
    const std::vector<std::string> outputs = Get(Connectable::kConnectionsOutputs);
    const size_t count = std::min({sources.size(), inputs.size(), outputs.size()});
    std::vector<Connection> connections;
    connections.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        connections.push_back({inputs[i], sources[i], outputs[i]});
    }
    return connections;
}

namespace {

void WriteConnections(const Prim& prim, const std::vector<Connection>& connections) {
    std::vector<std::string> sources;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    for (const Connection& connection : connections) {
        sources.push_back(connection.source);
        inputs.push_back(connection.input);
        outputs.push_back(connection.output);
    }
    prim.Set(Connectable::kConnectionsSources, sources);
    prim.Set(Connectable::kConnectionsInputs, inputs);
    prim.Set(Connectable::kConnectionsOutputs, outputs);
}

}

void Prim::Connect(std::string_view input, const Prim& source, std::string_view output) const {
    const auto lock = LockScene(m_scene);
    CheckSchema(SchemaId::Connectable);
    source.CheckSchema(SchemaId::Connectable);
    if (&source.GetScene() != &GetScene()) {
        throw Error("{} can't connect {} to {}, which is in another scene", Path(), input,
                    source.Path());
    }
    if (input.empty() || output.empty()) {
        throw Error("Connecting {} to {} needs both an input and an output name", Path(),
                    source.Path());
    }
    std::vector<Connection> connections = Connections();
    const auto found = std::ranges::find(connections, input, &Connection::input);
    if (found != connections.end()) {
        Log(LogLevel::Debug, "{}'s {} reconnects from {}.{} to {}.{}", Path(), input, found->source,
            found->output, source.Path(), output);
        found->source = source.Path();
        found->output = std::string(output);
    } else {
        Log(LogLevel::Debug, "{}'s {} connects to {}.{}", Path(), input, source.Path(), output);
        connections.push_back({std::string(input), source.Path(), std::string(output)});
    }
    WriteConnections(*this, connections);
}

void Prim::Disconnect(std::string_view input) const {
    const auto lock = LockScene(m_scene);
    std::vector<Connection> connections = Connections();
    const auto found = std::ranges::find(connections, input, &Connection::input);
    if (found == connections.end()) {
        return;
    }
    Log(LogLevel::Debug, "{}'s {} disconnects from {}.{}", Path(), input, found->source,
        found->output);
    connections.erase(found);
    WriteConnections(*this, connections);
}

View Prim::Single() const {
    const auto lock = LockScene(m_scene);
    return GetScene().Select(*this);
}

View Prim::Single(SchemaId api, std::string_view instance) const {
    const auto lock = LockScene(m_scene);
    return GetScene().Select(*this, api, instance);
}

std::vector<std::string> Prim::Instances(SchemaId api) const {
    const auto lock = LockScene(m_scene);
    Check();
    const Structure& structure = SceneImpl::Get(*m_scene).GetStructure();
    std::vector<std::string> names;
    for (const ApiInstance& entry : structure.GetPrims().Instances(m_index, api)) {
        names.emplace_back(structure.GetTokens().Text(Token{entry.instance}));
    }
    std::ranges::sort(names);
    return names;
}

bool Prim::IsA(SchemaId schema) const {
    const auto lock = LockScene(m_scene);
    return chrono::batch_render::IsA(Schema(), schema);
}

bool Prim::HasApi(SchemaId api) const {
    const auto lock = LockScene(m_scene);
    Check();
    return SceneImpl::Get(*m_scene).GetStructure().GetPrims().HasApi(m_index, api);
}

void Prim::CheckSchema(SchemaId schema) const {
    const auto lock = LockScene(m_scene);
    if (!chrono::batch_render::IsA(Schema(), schema) && !HasApi(schema)) {
        throw Error("{} is a {}, not a {}", Path(), Info(Schema()).name, Info(schema).name);
    }
}

Token Prim::Intern(std::string_view text) const {
    const auto lock = LockScene(m_scene);
    return GetScene().Token(text);
}

std::string_view Prim::Text(Token token) const {
    const auto lock = LockScene(m_scene);
    return GetScene().Text(token);
}

}
