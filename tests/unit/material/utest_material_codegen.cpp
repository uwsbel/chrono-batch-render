// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/material/codegen.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

namespace chrono::batch_render::material {
namespace {

TEST(MaterialCodegenTest, DispatchLinksOnlyTheBuiltinsWithoutGeneratedDefinitions) {
    const MaterialDefinitionSet builtins;
    const auto sources = MaterialDispatchSources(builtins);
    ASSERT_EQ(sources.size(), 1u);
    const std::string& dispatch = sources.at(kDispatchFile);
    EXPECT_NE(dispatch.find("return definition < kDefinitionHapke;"), std::string::npos);
    EXPECT_EQ(dispatch.find("import chrono_batch_render.material.generated"), std::string::npos);
    EXPECT_FALSE(builtins.Contains(kFirstPlanetaryDefinition));
    EXPECT_FALSE(builtins.Contains(kFirstGeneratedDefinition));
    EXPECT_TRUE(builtins.Contains(kFirstPlanetaryDefinition - 1));
}

TEST(MaterialCodegenTest, GeneratedDefinitionsAddAnImportACaseAndTheirModule) {
    const MaterialDefinitionSet set({{.id = kFirstGeneratedDefinition,
                                      .type = "Wet5",
                                      .module = "chrono_batch_render.material.generated.d5",
                                      .source = "struct Wet5 {}"}});
    const auto sources = MaterialDispatchSources(set);
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_EQ(sources.at("chrono_batch_render/material/generated/d5.slang"), "struct Wet5 {}");
    const std::string& dispatch = sources.at(kDispatchFile);
    EXPECT_NE(dispatch.find("import chrono_batch_render.material.generated.d5;"),
              std::string::npos);
    EXPECT_NE(dispatch.find(fmt::format("|| definition == {}", kFirstGeneratedDefinition)),
              std::string::npos);
    EXPECT_NE(dispatch.find("StandardDefinition<Wet5, Spectrum>.Shade(context, closure);"),
              std::string::npos);
    EXPECT_TRUE(set.Contains(kFirstGeneratedDefinition));
    EXPECT_FALSE(set.Contains(kFirstGeneratedDefinition + 1));
    EXPECT_NE(set.Hash(), MaterialDefinitionSet().Hash());
}

}
}
