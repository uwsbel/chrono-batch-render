// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/material/codegen.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

#include <fmt/format.h>

#include "chrono_batch_render/util/text.h"

extern "C" {
extern const unsigned char chrono_batch_render_material_dispatch[];
extern const std::size_t chrono_batch_render_material_dispatch_size;
extern const unsigned char chrono_batch_render_material_definition[];
extern const std::size_t chrono_batch_render_material_definition_size;
extern const unsigned char chrono_batch_render_material_surface[];
extern const std::size_t chrono_batch_render_material_surface_size;
extern const unsigned char chrono_batch_render_material_standard[];
extern const std::size_t chrono_batch_render_material_standard_size;
extern const unsigned char chrono_batch_render_material_registered[];
extern const std::size_t chrono_batch_render_material_registered_size;
}

namespace chrono::batch_render::material {

namespace {

std::string_view Embedded(const unsigned char* bytes, std::size_t size) {
    return {reinterpret_cast<const char*>(bytes), size};
}

std::string Cases(const MaterialDefinitionSet& set, std::string_view standard,
                  std::string_view surface) {
    std::string cases;
    for (const MaterialDefinition& definition : set.Definitions()) {
        const std::string_view call = definition.surface ? surface : standard;
        cases += fmt::format("    }} else if (context.definition == {}) {{\n        {}\n",
                             definition.id, fmt::format(fmt::runtime(call), definition.type));
    }
    return cases;
}

constexpr std::string_view kPlanetaryDefinitions[] = {"Hapke", "LommelSeeliger", "LunarLambert",
                                                      "OrenNayar"};

std::string PlanetaryCases(bool planetary, std::string_view call) {
    std::string cases;
    if (!planetary) {
        return cases;
    }
    for (const std::string_view name : kPlanetaryDefinitions) {
        cases +=
            fmt::format("    }} else if (context.definition == kDefinition{0}) {{\n        {1}\n",
                        name, fmt::format(fmt::runtime(call), name));
    }
    return cases;
}

std::string Dispatch(const MaterialDefinitionSet& set) {
    std::string imports;
    std::string linked;
    for (const MaterialDefinition& definition : set.Definitions()) {
        imports += fmt::format("import {};\n", definition.module);
        linked += fmt::format(" || definition == {}", definition.id);
    }
    const bool planetary = set.Planetary();
    return FillTemplate(
        Embedded(chrono_batch_render_material_dispatch, chrono_batch_render_material_dispatch_size),
        {{"imports", imports},
         {"planetaryImport",
          planetary ? "__exported import "
                      "chrono_batch_render.render.kernels.material.definitions.planetary;\n"
                    : ""},
         {"planetaryLinked",
          planetary ? " || (definition >= kDefinitionHapke && definition <= kDefinitionOrenNayar)"
                    : ""},
         {"planetaryShade",
          PlanetaryCases(planetary, "{}Definition<Spectrum>.Shade(context, closure);")},
         {"planetaryAlbedo",
          PlanetaryCases(planetary, "return {}Definition<Spectrum>.Albedo(context);")},
         {"linked", linked},
         {"shadeCases", Cases(set, "StandardDefinition<{}, Spectrum>.Shade(context, closure);",
                              "{}<Spectrum>.Shade(context, closure);")},
         {"albedoCases", Cases(set, "return StandardDefinition<{}, Spectrum>.Albedo(context);",
                               "return {}<Spectrum>.Albedo(context);")}});
}

std::string Expression(const ShadingGraph& graph, const GraphLayout& layout, uint32_t node,
                       uint32_t input) {
    const InputBinding& binding = graph.nodes[node].inputs[input];
    const MaterialNodeInput& declared = graph.nodes[node].node->inputs[input];
    const bool spectral = declared.type == MaterialInputType::Spectral;
    if (binding.kind == BindingKind::Pattern) {
        return spectral ? fmt::format("SpectralValue.Rgb(r{})", binding.source)
                        : fmt::format("r{}", binding.source);
    }
    const std::string cell = fmt::format("p.n{}_{}", node, declared.name);
    if (binding.kind == BindingKind::Texture) {
        const std::string sampled =
            fmt::format("texels.texels[{0}], texels.Handle({0}), &p.textures[{0}]",
                        layout.textures[node][input]);
        if (declared.type == MaterialInputType::Float) {
            return fmt::format("TexturedFloat({}, {})", cell, sampled);
        }
        const std::string color = fmt::format("TexturedColor({}, {})", cell, sampled);
        return spectral ? fmt::format("SpectralValue.Rgb({})", color) : color;
    }
    if (spectral) {
        return fmt::format("({0}_spectrum != kNoSpectrum ? SpectralValue.Library({0}_spectrum) : "
                           "SpectralValue.Rgb({0}.Vector()))",
                           cell);
    }
    switch (declared.type) {
    case MaterialInputType::Bool:
        return cell + " != 0";
    case MaterialInputType::Float2:
    case MaterialInputType::Float3:
    case MaterialInputType::Float4:
        return cell + ".Vector()";
    default:
        return cell;
    }
}

std::string Call(const ShadingGraph& graph, const GraphLayout& layout, uint32_t node) {
    const MaterialNodeDefinition& definition = *graph.nodes[node].node;
    std::vector<std::string> arguments;
    for (const MaterialNodeParameter& parameter : definition.parameters) {
        switch (parameter.source) {
        case MaterialNodeParameter::Source::Context:
            arguments.emplace_back("context");
            break;
        case MaterialNodeParameter::Source::Point:
            arguments.emplace_back("context.point");
            break;
        case MaterialNodeParameter::Source::Input:
            arguments.push_back(Expression(graph, layout, node, parameter.input));
            break;
        }
    }
    return fmt::format("        const {} r{} = {}({});\n", InputInfo(definition.result).slangType,
                       node, definition.name, Join(arguments));
}

std::string ParamsFields(const ShadingGraph& graph, const GraphLayout& layout) {
    std::string fields;
    if (layout.textureCount > 0) {
        fields += fmt::format("    TextureInput textures[{}];\n", layout.textureCount);
    }
    if (layout.mapped) {
        fields += "    MappingRecord mapping;\n";
    }
    if (layout.detailed) {
        fields += fmt::format("    DetailRecord detail;\n    TextureInput details[{}];\n",
                              kDetailTextures);
    }
    for (uint32_t node = 0; node < graph.nodes.size(); ++node) {
        for (const MaterialNodeInput& input : graph.nodes[node].node->inputs) {
            fields +=
                fmt::format("    {} n{}_{};\n", InputInfo(input.type).cellType, node, input.name);
            if (input.type == MaterialInputType::Spectral) {
                fields += fmt::format("    uint n{}_{}_spectrum;\n", node, input.name);
            }
        }
    }
    return fields;
}

std::string AddImports(const ShadingGraph& graph, MaterialDefinition& module) {
    std::string imports;
    for (const ShadingNode& node : graph.nodes) {
        const MaterialNodeDefinition& definition = *node.node;
        const std::string file = ModulePath(definition.module);
        if (module.files.contains(file)) {
            continue;
        }
        imports += fmt::format("import {};\n", definition.module);
        if (definition.source.empty()) {
            continue;
        }
        module.files.emplace(file, definition.source);
        module.origins.emplace_back(file, definition.file);
        module.origins.emplace_back(definition.module + ".slang", definition.file);
    }
    return imports;
}

std::string Body(const ShadingGraph& graph, const GraphLayout& layout) {
    const auto surface = uint32_t(graph.nodes.size() - 1);
    const MaterialNodeDefinition& top = *graph.nodes[surface].node;
    std::string body;
    if (layout.detailed) {
        body += "        SurfacePoint point = context.point;\n"
                "        const float3 overlay = MapNodeDetail(context.materials, &p.mapping, "
                "&p.detail, &p.details[0], point);\n"
                "        context.point = point;\n";
    }
    if (layout.textureCount > 0) {
        body += fmt::format("        const SurfaceTexels<{0}> texels = SampleNodeTextures<{0}>("
                            "context, &p.mapping, &p.textures[0]);\n",
                            layout.textureCount);
    }
    for (uint32_t node = 0; node < surface; ++node) {
        body += Call(graph, layout, node);
    }
    body += fmt::format("        {} node;\n", top.name);
    for (uint32_t input = 0; input < top.inputs.size(); ++input) {
        const std::string value = Expression(graph, layout, surface, input);
        const bool overlaid = layout.detailed && top.detailAlbedo == input &&
                              top.inputs[input].type == MaterialInputType::Spectral;
        body += fmt::format("        node.{} = {};\n", top.inputs[input].name,
                            overlaid ? fmt::format("DetailedAlbedo({}, overlay)", value) : value);
    }
    return body;
}

}

bool Texturable(MaterialInputType type) {
    return type == MaterialInputType::Float || type == MaterialInputType::Float3 ||
           type == MaterialInputType::Spectral;
}

GraphLayout LayOut(const ShadingGraph& graph) {
    GraphLayout layout;
    for (const ShadingNode& node : graph.nodes) {
        std::vector<uint32_t>& textures = layout.textures.emplace_back();
        for (const InputBinding& binding : node.inputs) {
            textures.push_back(binding.kind == BindingKind::Texture ? layout.textureCount++
                                                                    : kNoTexture);
        }
    }
    uint32_t offset = 0;
    layout.detailed = !graph.nodes.empty() && graph.nodes.back().node->detail;
    layout.mapped = layout.textureCount > 0 || layout.detailed;
    if (layout.mapped) {
        layout.mapping = layout.textureCount * kTextureInputBytes;
        offset = layout.mapping + kMappingBytes;
    }
    if (layout.detailed) {
        layout.detail = offset;
        layout.detailTextures = offset + kDetailBytes;
        offset = layout.detailTextures + kDetailTextures * kTextureInputBytes;
    }
    for (const ShadingNode& node : graph.nodes) {
        std::vector<uint32_t>& cells = layout.cells.emplace_back();
        for (const MaterialNodeInput& input : node.node->inputs) {
            cells.push_back(offset);
            offset += InputInfo(input.type).bytes;
        }
    }
    layout.bytes = offset;
    return layout;
}

graph::ShapeKey MaterialDefinitionKey(const ShadingGraph& graph) {
    graph::ShapeWriter writer;
    writer.Add(uint32_t(graph.nodes.size()));
    for (const ShadingNode& node : graph.nodes) {
        writer.Add(node.node->module);
        writer.Add(node.node->name);
        writer.Add(uint32_t(node.inputs.size()));
        for (const InputBinding& binding : node.inputs) {
            writer.Add(uint32_t(binding.kind));
            if (binding.kind == BindingKind::Pattern) {
                writer.Add(binding.source);
            }
        }
    }
    return std::move(writer).Finish();
}

MaterialDefinition GenerateMaterialDefinition(uint32_t id, const ShadingGraph& graph) {
    const GraphLayout layout = LayOut(graph);
    const MaterialNodeDefinition& top = *graph.nodes.back().node;
    MaterialDefinition module{.id = id,
                              .type = fmt::format("Definition{}", id),
                              .module =
                                  fmt::format("chrono_batch_render.material.generated.d{}", id),
                              .words = layout.bytes / uint32_t(sizeof(uint32_t)),
                              .surface = top.role == MaterialNodeRole::Surface};
    const std::string& type = module.type;
    const std::string imports = AddImports(graph, module);
    const std::string fields = ParamsFields(graph, layout);
    const std::string body = Body(graph, layout);
    const std::string definition =
        FillTemplate(module.surface ? Embedded(chrono_batch_render_material_surface,
                                               chrono_batch_render_material_surface_size)
                                    : Embedded(chrono_batch_render_material_standard,
                                               chrono_batch_render_material_standard_size),
                     {{"type", type}, {"body", body}});
    module.source = FillTemplate(Embedded(chrono_batch_render_material_definition,
                                          chrono_batch_render_material_definition_size),
                                 {{"node", top.name},
                                  {"imports", imports},
                                  {"type", type},
                                  {"fields", fields},
                                  {"definition", definition}});
    return module;
}

std::map<std::string, std::string, std::less<>>
ClosurePrimitiveSources(std::span<const std::shared_ptr<const ClosurePrimitive>> primitives) {
    std::map<std::string, std::string, std::less<>> sources;
    if (primitives.empty()) {
        return sources;
    }
    std::string imports;
    std::string kinds;
    std::string glossy;
    std::string estimate;
    std::string evaluate;
    std::string pdf;
    std::string direction;
    for (const std::shared_ptr<const ClosurePrimitive>& primitive : primitives) {
        sources[ModulePath(primitive->module)] = primitive->source;
        imports += fmt::format("import {};\n", primitive->module);
        kinds += fmt::format("static const uint kLobe{} = {};\n", primitive->name, primitive->kind);
        const auto add = [&](std::string& cases, std::string_view call) {
            cases += fmt::format("    case {}:\n        return {}.{};\n", primitive->kind,
                                 primitive->name, call);
        };
        add(glossy, "Glossy()");
        add(estimate, "Estimate<Spectrum>(tint, alpha, params.values, outgoing)");
        add(evaluate, "Evaluate<Spectrum>(tint, alpha, params.values, outgoing, incoming)");
        add(pdf, "Pdf(alpha, params.values, outgoing, incoming)");
        add(direction, "Direction(alpha, params.values, outgoing, u)");
    }
    sources[kRegisteredFile] = FillTemplate(Embedded(chrono_batch_render_material_registered,
                                                     chrono_batch_render_material_registered_size),
                                            {{"imports", imports},
                                             {"firstLobe", std::to_string(kFirstPrimitiveLobe)},
                                             {"kinds", kinds},
                                             {"glossy", glossy},
                                             {"estimate", estimate},
                                             {"evaluate", evaluate},
                                             {"pdf", pdf},
                                             {"direction", direction}});
    return sources;
}

std::map<std::string, std::string, std::less<>>
MaterialDispatchSources(const MaterialDefinitionSet& set) {
    std::map<std::string, std::string, std::less<>> sources(set.Files().begin(), set.Files().end());
    sources[kDispatchFile] = Dispatch(set);
    if (set.Planetary()) {
        sources[kLobesFile] = "static const bool kPhotometricLobes = true;\n";
    }
    for (const MaterialDefinition& definition : set.Definitions()) {
        sources[ModulePath(definition.module)] = definition.source;
        for (const auto& [path, source] : definition.files) {
            sources[path] = source;
        }
    }
    return sources;
}

MaterialDefinitionSet::MaterialDefinitionSet(std::vector<MaterialDefinition> definitions,
                                             std::map<std::string, std::string, std::less<>> files,
                                             bool planetary)
    : m_definitions(std::move(definitions)), m_files(std::move(files)), m_planetary(planetary),
      m_hash(kFnvBasis) {
    m_hash = Fnv1a(planetary ? "planetary" : "", m_hash);
    for (const auto& [path, source] : m_files) {
        m_hash = Fnv1a(path, m_hash);
        m_hash = Fnv1a(source, m_hash);
    }
    std::ranges::sort(m_definitions, {}, &MaterialDefinition::id);
    for (const MaterialDefinition& definition : m_definitions) {
        m_hash = Fnv1a(std::to_string(definition.id), m_hash);
        m_hash = Fnv1a(definition.type, m_hash);
        m_hash = Fnv1a(definition.source, m_hash);
        for (const auto& [path, source] : definition.files) {
            m_hash = Fnv1a(path, m_hash);
            m_hash = Fnv1a(source, m_hash);
        }
    }
}

bool MaterialDefinitionSet::Contains(uint32_t id) const {
    if (id >= kFirstPlanetaryDefinition && id < kFirstGeneratedDefinition) {
        return m_planetary;
    }
    return id < kFirstGeneratedDefinition ||
           std::ranges::binary_search(m_definitions, id, {}, &MaterialDefinition::id);
}

}
