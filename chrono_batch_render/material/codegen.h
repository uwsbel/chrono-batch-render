// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "chrono_batch_render/graph/graph.h"
#include "chrono_batch_render/material/node.h"

namespace chrono::batch_render::material {

inline constexpr uint32_t kFirstPlanetaryDefinition = 4;
inline constexpr uint32_t kFirstGeneratedDefinition = 8;

struct MaterialDefinition {
    uint32_t id = 0;
    std::string type;
    std::string module;
    std::string source;
    std::map<std::string, std::string, std::less<>> files;
    std::vector<std::pair<std::string, std::string>> origins;
    uint32_t words = 0;
    bool surface = false;
};

class MaterialDefinitionSet {
  public:
    explicit MaterialDefinitionSet(std::vector<MaterialDefinition> definitions = {},
                                   std::map<std::string, std::string, std::less<>> files = {},
                                   bool planetary = false);

    std::span<const MaterialDefinition> Definitions() const {
        return m_definitions;
    }

    const std::map<std::string, std::string, std::less<>>& Files() const {
        return m_files;
    }

    uint64_t Hash() const {
        return m_hash;
    }

    bool Planetary() const {
        return m_planetary;
    }

    bool Contains(uint32_t id) const;

  private:
    std::vector<MaterialDefinition> m_definitions;
    std::map<std::string, std::string, std::less<>> m_files;
    bool m_planetary = false;
    uint64_t m_hash = 0;
};

using MaterialDefinitionSetRef = std::shared_ptr<const MaterialDefinitionSet>;

inline constexpr const char* kDispatchFile =
    "chrono_batch_render/render/kernels/material/dispatch.slang";

inline constexpr const char* kLobesFile = "chrono_batch_render/render/kernels/material/lobes.slang";

inline constexpr uint32_t kTextureInputBytes = 48;
inline constexpr uint32_t kMappingBytes = 64;
inline constexpr uint32_t kDetailBytes = 80;
inline constexpr uint32_t kDetailTextures = 3;
inline constexpr uint32_t kNoTexture = 0xFFFFFFFF;

enum class BindingKind : uint8_t { Value, Texture, Pattern };

struct InputBinding {
    BindingKind kind = BindingKind::Value;
    uint32_t source = 0;
};

struct ShadingNode {
    std::shared_ptr<const MaterialNodeDefinition> node;
    std::vector<InputBinding> inputs;
};

struct ShadingGraph {
    std::vector<ShadingNode> nodes;
};

struct GraphLayout {
    std::vector<std::vector<uint32_t>> cells;
    std::vector<std::vector<uint32_t>> textures;
    uint32_t textureCount = 0;
    bool mapped = false;
    uint32_t mapping = 0;
    bool detailed = false;
    uint32_t detail = 0;
    uint32_t detailTextures = 0;
    uint32_t bytes = 0;
};

bool Texturable(MaterialInputType type);

GraphLayout LayOut(const ShadingGraph& graph);

graph::ShapeKey MaterialDefinitionKey(const ShadingGraph& graph);

MaterialDefinition GenerateMaterialDefinition(uint32_t id, const ShadingGraph& graph);

inline constexpr const char* kRegisteredFile =
    "chrono_batch_render/render/kernels/material/registered.slang";

std::map<std::string, std::string, std::less<>>
ClosurePrimitiveSources(std::span<const std::shared_ptr<const ClosurePrimitive>> primitives);

std::map<std::string, std::string, std::less<>>
MaterialDispatchSources(const MaterialDefinitionSet& set);

}
