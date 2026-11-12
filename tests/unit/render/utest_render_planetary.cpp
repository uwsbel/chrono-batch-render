// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <regex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/io/image.h"
#include "chrono_batch_render/material/codegen.h"
#include "chrono_batch_render/render/kernels.h"
#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/records/materials.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/error.h"
#include "support/buffers.h"
#include "support/devices.h"
#include "support/support.h"

namespace chrono::batch_render {
namespace {

using device::Device;
using test::Download;
using test::ForEachDevice;

constexpr double kPiD = 3.14159265358979323846;

enum Model : uint32_t { kHapke, kLommelSeeliger, kLunarLambert, kOrenNayar };

struct Geometry {
    double mu0;
    double mu;
    double cosG;
    double cosPsi;
    Float3 outgoing;
    Float3 incoming;
};

std::vector<Geometry> Grid() {
    std::vector<Geometry> grid;
    for (const double i : {0.0, 10.0, 30.0, 50.0, 70.0, 85.0}) {
        for (const double e : {0.0, 15.0, 30.0, 60.0, 80.0}) {
            for (const double psi : {0.0, 45.0, 90.0, 135.0, 180.0}) {
                const double ri = i * kPiD / 180;
                const double re = e * kPiD / 180;
                const double rp = psi * kPiD / 180;
                const Float3 incoming{float(std::sin(ri)), 0, float(std::cos(ri))};
                const Float3 outgoing{float(std::sin(re) * std::cos(rp)),
                                      float(std::sin(re) * std::sin(rp)), float(std::cos(re))};
                grid.push_back(
                    {std::cos(ri), std::cos(re),
                     std::cos(ri) * std::cos(re) + std::sin(ri) * std::sin(re) * std::cos(rp),
                     i == 0 || e == 0 ? 1.0 : std::cos(rp), outgoing, incoming});
            }
        }
    }
    return grid;
}

double HenyeyGreenstein(double xi, double cosG) {
    return (1 - xi * xi) / std::pow(1 + 2 * xi * cosG + xi * xi, 1.5);
}

double HapkeH(double w, double x) {
    const double gamma = std::sqrt(1 - w);
    const double r0 = (1 - gamma) / (1 + gamma);
    return 1 / (1 - w * x * (r0 + 0.5 * (1 - 2 * r0 * x) * std::log((1 + x) / x)));
}

double Porosity(double phi) {
    const double q = 1.209 * std::pow(std::clamp(phi, 1e-6, 0.74), 2.0 / 3.0);
    return -std::log(1 - q) / q;
}

struct HapkeInputs {
    double b = 0.25;
    double c = 0.5;
    double bs0 = 1;
    double hs = 0.07;
    double thetaBar = 20;
    double phi = 0.3;
};

double HapkeRadianceFactor(const Geometry& g, double w, const HapkeInputs& p) {
    const double thetaBar = p.thetaBar * kPiD / 180;
    const double k = Porosity(p.phi);
    double mu0e = g.mu0;
    double mue = g.mu;
    double s = 1;
    if (thetaBar >= 1e-4) {
        const double tanT = std::tan(thetaBar);
        const double chi = 1 / std::sqrt(1 + kPiD * tanT * tanT);
        const double i = std::max(std::acos(g.mu0), 1e-4);
        const double e = std::max(std::acos(g.mu), 1e-4);
        const double psi = std::min(std::acos(std::clamp(g.cosPsi, -1.0, 1.0)), kPiD - 1e-4);
        const double f = std::exp(-2 * std::tan(psi / 2));
        const double sin2 = std::pow(std::sin(psi / 2), 2.0);
        const auto e1 = [&](double x) { return std::exp(-2 / (kPiD * tanT * std::tan(x))); };
        const auto e2 = [&](double x) {
            return std::exp(-1 / (kPiD * tanT * tanT * std::tan(x) * std::tan(x)));
        };
        const auto eta = [&](double x) {
            return chi * (std::cos(x) + std::sin(x) * tanT * e2(x) / (2 - e1(x)));
        };
        if (i <= e) {
            const double d = 2 - e1(e) - psi / kPiD * e1(i);
            mu0e = chi *
                   (std::cos(i) + std::sin(i) * tanT * (std::cos(psi) * e2(e) + sin2 * e2(i)) / d);
            mue = chi * (std::cos(e) + std::sin(e) * tanT * (e2(e) - sin2 * e2(i)) / d);
            s = mue / eta(e) * g.mu0 / eta(i) * chi / (1 - f + f * chi * g.mu0 / eta(i));
        } else {
            const double d = 2 - e1(i) - psi / kPiD * e1(e);
            mu0e = chi * (std::cos(i) + std::sin(i) * tanT * (e2(i) - sin2 * e2(e)) / d);
            mue = chi *
                  (std::cos(e) + std::sin(e) * tanT * (std::cos(psi) * e2(i) + sin2 * e2(e)) / d);
            s = mue / eta(e) * g.mu0 / eta(i) * chi / (1 - f + f * chi * g.mu / eta(e));
        }
    }
    const double angle = std::acos(std::clamp(g.cosG, -1.0, 1.0));
    const double b2 = p.b * p.b;
    const double phase = 0.5 * (1 + p.c) * (1 - b2) / std::pow(1 - 2 * p.b * g.cosG + b2, 1.5) +
                         0.5 * (1 - p.c) * (1 - b2) / std::pow(1 + 2 * p.b * g.cosG + b2, 1.5);
    const double shoe = p.bs0 / (1 + std::tan(angle / 2) / p.hs);
    const double multiple = HapkeH(w, mu0e / k) * HapkeH(w, mue / k) - 1;
    return k * w / 4 * mu0e / (mu0e + mue) * (phase * (1 + shoe) + multiple) * s;
}

double Brdf(Model model, const Geometry& g, double albedo, const Float4& params, double alpha,
            const HapkeInputs& hapke) {
    switch (model) {
    case kHapke:
        return HapkeRadianceFactor(g, albedo, hapke) / (kPiD * g.mu0);
    case kLommelSeeliger:
        return albedo * HenyeyGreenstein(params.x, g.cosG) / (4 * kPiD * (g.mu0 + g.mu));
    case kLunarLambert: {
        const double angle = std::acos(std::clamp(g.cosG, -1.0, 1.0)) * 180 / kPiD;
        const double l = 1 + angle * (params.x + angle * (params.y + angle * params.z));
        return albedo * std::max(2 * l / (g.mu0 + g.mu) + (1 - l), 0.0) / kPiD;
    }
    case kOrenNayar: {
        const double sigma2 = alpha * alpha;
        const double a = 1 - 0.5 * sigma2 / (sigma2 + 0.33);
        const double b = 0.45 * sigma2 / (sigma2 + 0.09);
        const double i = std::acos(g.mu0);
        const double e = std::acos(g.mu);
        return albedo / kPiD *
               (a +
                b * std::max(0.0, g.cosPsi) * std::sin(std::max(i, e)) * std::tan(std::min(i, e)));
    }
    }
    return 0;
}

constexpr const char* kKernel = R"(
import chrono_batch_render.render.kernels.material.primitives.hapke;
import chrono_batch_render.render.kernels.material.primitives.lommel_seeliger;
import chrono_batch_render.render.kernels.material.primitives.lunar_lambert;
import chrono_batch_render.render.kernels.material.primitives.oren_nayar;
import chrono_batch_render.render.kernels.spectrum.rgb;

struct PlanetaryParams {
    float4* cases;
    float4* results;
    uint count;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void EvaluatePlanetary(uniform PlanetaryParams p, uint3 id: SV_DispatchThreadID) {
    if (id.x >= p.count) {
        return;
    }
    const float4 outgoing = p.cases[4 * id.x];
    const float4 incoming = p.cases[4 * id.x + 1];
    const float4 params = p.cases[4 * id.x + 2];
    const float4 tint = p.cases[4 * id.x + 3];
    const RgbSpectrum albedo = RgbSpectrum(tint.xyz);
    const float alpha = outgoing.w;
    RgbSpectrum value = RgbSpectrum(float3(0));
    float pdf = 0;
    switch (uint(incoming.w)) {
    case 0: {
        const float4 packed = HapkeParams(params.x, params.y, params.z, params.w, tint.w);
        const HapkeBsdf<RgbSpectrum> bsdf = HapkeBsdf<RgbSpectrum>(albedo, alpha, packed);
        value = bsdf.Evaluate(outgoing.xyz, incoming.xyz);
        pdf = bsdf.Pdf(incoming.xyz);
        break;
    }
    case 1: {
        const LommelSeeligerBsdf<RgbSpectrum> bsdf = LommelSeeligerBsdf<RgbSpectrum>(albedo, params.x);
        value = bsdf.Evaluate(outgoing.xyz, incoming.xyz);
        pdf = bsdf.Pdf(incoming.xyz);
        break;
    }
    case 2: {
        const LunarLambertBsdf<RgbSpectrum> bsdf = LunarLambertBsdf<RgbSpectrum>(albedo, params.xyz);
        value = bsdf.Evaluate(outgoing.xyz, incoming.xyz);
        pdf = bsdf.Pdf(incoming.xyz);
        break;
    }
    default: {
        const OrenNayarBsdf<RgbSpectrum> bsdf = OrenNayarBsdf<RgbSpectrum>(albedo, alpha);
        value = bsdf.Evaluate(outgoing.xyz, incoming.xyz);
        pdf = bsdf.Pdf(incoming.xyz);
        break;
    }
    }
    p.results[id.x] = float4(value.values, pdf);
}
)";

struct PlanetaryParams {
    uint64_t cases;
    uint64_t results;
    uint32_t count;
};

struct Case {
    Model model;
    Geometry geometry;
    Float4 params;
    float alpha;
    HapkeInputs hapke;
};

TEST(PlanetaryMaterialTest, PrimitivesMatchTheirPublishedFormulas) {
    ForEachDevice([](Device& device) {
        const Ref<device::Program> program =
            device.CreateProgram({.modules = {"planetary_test"},
                                  .library = &kRenderKernels,
                                  .sources = {{"planetary_test.slang", kKernel}},
                                  .entries = {"EvaluatePlanetary"},
                                  .label = "planetary primitives"});
        device::Kernel& kernel = program->Entry("EvaluatePlanetary");
        kernel.Wait();

        const HapkeInputs rough;
        const HapkeInputs smooth{
            .b = 0.4, .c = -0.3, .bs0 = 2.5, .hs = 0.05, .thetaBar = 0, .phi = 0.1};
        std::vector<Case> cases;
        for (const Geometry& g : Grid()) {
            for (const HapkeInputs& h : {rough, smooth}) {
                cases.push_back({kHapke,
                                 g,
                                 {float(h.b), float(h.c), float(h.phi), float(h.bs0)},
                                 float(h.thetaBar * kPiD / 180),
                                 h});
            }
            cases.push_back({kLommelSeeliger, g, {-0.3f, 0, 0, 0}, 1, {}});
            cases.push_back({kLunarLambert, g, {-0.019f, 0.000242f, -0.00000146f, 0}, 1, {}});
            cases.push_back({kOrenNayar, g, {}, 0, {}});
            cases.push_back({kOrenNayar, g, {}, float(30 * kPiD / 180), {}});
        }
        const Float3 albedo{0.12f, 0.35f, 0.8f};
        std::vector<Float4> words;
        for (const Case& c : cases) {
            const Geometry& g = c.geometry;
            words.push_back({g.outgoing.x, g.outgoing.y, g.outgoing.z, c.alpha});
            words.push_back({g.incoming.x, g.incoming.y, g.incoming.z, float(c.model)});
            words.push_back(c.params);
            words.push_back({albedo.x, albedo.y, albedo.z, float(c.hapke.hs)});
        }
        const Ref<device::Buffer> input = device.CreateBuffer({.label = "cases"}, words);
        const Ref<device::Buffer> output =
            device.CreateBuffer({.label = "results"}, std::vector<Float4>(cases.size()));
        const PlanetaryParams params{input->Address(), output->Address(), uint32_t(cases.size())};
        device::CommandEncoder encoder = device.GetQueue().CreateEncoder();
        encoder.Dispatch(kernel, params, {.x = device::GroupCount(uint32_t(cases.size()), 64)});
        device.GetQueue().Submit(encoder.Finish());
        const std::vector<Float4> results = Download<Float4>(device, *output, cases.size());

        for (size_t n = 0; n < cases.size(); ++n) {
            const Case& c = cases[n];
            const Geometry& g = c.geometry;
            const float gpu[] = {results[n].x, results[n].y, results[n].z};
            const float channels[] = {albedo.x, albedo.y, albedo.z};
            for (int ch = 0; ch < 3; ++ch) {
                const double expected = Brdf(c.model, g, channels[ch], c.params, c.alpha, c.hapke);
                EXPECT_NEAR(gpu[ch], expected, 2e-3 * std::abs(expected) + 1e-5)
                    << "model " << c.model << " case " << n << " channel " << ch << " mu0 " << g.mu0
                    << " mu " << g.mu << " cosPsi " << g.cosPsi;
            }
            EXPECT_NEAR(results[n].w, g.mu0 / kPiD, 1e-5) << "case " << n;
        }
    });
}

constexpr uint32_t kSide = 8;

Ref<Scene> LitQuad(Device& device) {
    const Ref<Scene> scene = Scene::Create(device, {.waitForKernels = true});
    scene->Define<Camera>("/Cam", {.translate = {0, 0, 5},
                                   .orient = LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0}),
                                   .focalLength = 30});
    scene->Define<Scope>("/Render");
    scene->Define<RenderVar>("/Render/Color", {.sourceName = "HdrColor"});
    scene->Define<RenderProduct>(
        "/Render/View",
        {.camera = {"/Cam"}, .resolution = {kSide, kSide}, .orderedVars = {"/Render/Color"}});
    scene->Find("/Render/View")->Set(MinimalAPI::kMode, MinimalMode::Full);
    scene->Define<Scope>("/Looks");
    const std::vector<Float2> st = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    scene->Define<Mesh>("/Quad", {.materialBinding = {"/Looks/Paint"},
                                  .primvars = {Primvar::Create("st", Interpolation::Vertex, st)},
                                  .points = {{-9, -9, 0}, {9, -9, 0}, {9, 9, 0}, {-9, 9, 0}},
                                  .faceVertexIndices = {0, 1, 2, 3},
                                  .faceVertexCounts = {4},
                                  .subdivisionScheme = SubdivisionScheme::None});
    return scene;
}

Float3 Center(Renderer& renderer) {
    RenderOutput output = renderer.Step("/Render/View");
    output.Wait();
    const std::vector<float> color =
        output["/Render/View"].frames.at(0).Source("HdrColor").tensor.ToHost<float>();
    const size_t at = 4 * ((kSide / 2) * kSide + kSide / 2);
    return {color[at], color[at + 1], color[at + 2]};
}

TEST(PlanetaryMaterialTest, BuiltinSurfacesShadeWithTheirInputs) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = LitQuad(device);
        const Prim material = scene->Define<Material>("/Looks/Paint");
        const Prim surface = scene->Define<Shader>("/Looks/Paint/Surface", {.id = "HapkeSurface"});
        material.Connect("surface", surface, "surface");
        EXPECT_EQ(surface.Get<float>("thetaBar"), 20.0f);
        Renderer renderer(scene);
        const Float3 grey = Center(renderer);
        EXPECT_GT(grey.y, 0.01f);
        EXPECT_NEAR(grey.x, grey.z, 1e-3f);
        surface.Set("albedo", Float3{0.1f, 0.3f, 0.9f});
        const Float3 blue = Center(renderer);
        EXPECT_GT(blue.z, blue.y);
        EXPECT_GT(blue.y, blue.x);
        for (const char* id :
             {"LommelSeeligerSurface", "LunarLambertSurface", "OrenNayarSurface"}) {
            surface.Set(Shader::kId, std::string(id));
            const Float3 pixel = Center(renderer);
            EXPECT_GT(pixel.y, 0.01f) << id;
        }
    });
}

TEST(PlanetaryMaterialTest, SurfaceDetailBendsNormalsAndTintsTheAlbedo) {
    const std::filesystem::path red =
        std::filesystem::temp_directory_path() / "chrono_batch_render_render_planetary_red.png";
    WritePng(red, Image(4, 4, Rgba{0.9f, 0.1f, 0.1f, 1}));
    ForEachDevice([&](Device& device) {
        const Ref<Scene> scene = LitQuad(device);
        const Prim material = scene->Define<Material>("/Looks/Paint");
        const Prim surface = scene->Define<Shader>("/Looks/Paint/Surface", {.id = "HapkeSurface"});
        material.Connect("surface", surface, "surface");
        Renderer renderer(scene);
        const Float3 flat = Center(renderer);
        ASSERT_GT(flat.y, 0.01f);

        surface.Set("normal", Float3{0.7f, 0, 0.7f});
        EXPECT_NEAR(Center(renderer).y, flat.y, 0.05f * flat.y);
        surface.Set("normal", Float3{0, 0, 1});

        surface.Set(Shader::kId, std::string("OrenNayarSurface"));
        surface.Set("sigma", 0.0f);
        const Float3 lambert = Center(renderer);
        surface.Set("normal", Float3{0.7f, 0, 0.7f});
        EXPECT_NEAR(Center(renderer).y, lambert.y * std::sqrt(0.5f), 0.05f * lambert.y);
        surface.Set("normal", Float3{0, 0, 1});
        EXPECT_NEAR(Center(renderer).y, lambert.y, 1e-3f);
        surface.Set(Shader::kId, std::string("HapkeSurface"));
        EXPECT_NEAR(Center(renderer).y, flat.y, 1e-3f);

        surface.Set("enableDetail", true);
        surface.Set("detailAlbedo", Float3{0.25f, 0.25f, 0.25f});
        surface.Set("detailAlbedoStrength", 1.0f);
        const Float3 detailed = Center(renderer);
        EXPECT_LT(detailed.y, 0.8f * flat.y);
        surface.Set("enableDetail", false);

        surface.Set(Shader::kId, std::string("LommelSeeligerSurface"));
        const Float3 lommel = Center(renderer);
        surface.Set("enableDetail", true);
        EXPECT_LT(Center(renderer).y, 0.8f * lommel.y);
        surface.Set("enableDetail", false);
        surface.Set(Shader::kId, std::string("HapkeSurface"));

        const Prim placement =
            scene->Define<Shader>("/Looks/Paint/Placement", {.id = "TexturePlacement"});
        placement.Set("hexTiling", true);
        placement.Set("uvScale", Float2{4, 4});
        const Prim texture = scene->Define<Shader>("/Looks/Paint/W", {.id = "UsdUVTexture"});
        texture.Set(UsdUVTexture::kFile, red.string());
        texture.Connect("st", placement, "st");
        surface.Connect("albedo", texture, "rgb");
        Float3 pixel = Center(renderer);
        for (int attempt = 0; attempt < 500 && pixel.x <= pixel.z; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            pixel = Center(renderer);
        }
        EXPECT_GT(pixel.x, 2 * pixel.z);
    });
}

Float3 Settled(Renderer& renderer, bool red) {
    Float3 pixel = Center(renderer);
    for (int attempt = 0; attempt < 500 && (pixel.x > 2 * pixel.z) != red; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        pixel = Center(renderer);
    }
    return pixel;
}

TEST(PlanetaryMaterialTest, TexturesFromMemoryFeedSurfaceInputs) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = LitQuad(device);
        const Prim material = scene->Define<Material>("/Looks/Paint");
        const Prim surface = scene->Define<Shader>("/Looks/Paint/Surface", {.id = "HapkeSurface"});
        material.Connect("surface", surface, "surface");

        std::vector<float> reddish(4 * 4 * 3);
        for (size_t i = 0; i < reddish.size(); i += 3) {
            reddish[i] = 0.9f;
            reddish[i + 1] = 0.1f;
            reddish[i + 2] = 0.1f;
        }
        scene->AddTexture(
            "memory/w.rgb",
            {.width = 4, .height = 4, .channels = 3, .texels = std::as_bytes(std::span(reddish))});
        const Prim texture = scene->Define<Shader>("/Looks/Paint/W", {.id = "UsdUVTexture"});
        texture.Set(UsdUVTexture::kFile, std::string("memory/w.rgb"));
        texture.Set(UsdUVTexture::kSourceColorSpace, TextureColorSpace::Raw);
        surface.Connect("albedo", texture, "rgb");
        Renderer renderer(scene);
        const Float3 red = Settled(renderer, true);
        EXPECT_GT(red.x, 2 * red.z);

        const std::vector<uint8_t> grey(8 * 8, 128);
        scene->AddTexture("memory/w.grey", {.width = 8,
                                            .height = 8,
                                            .channels = 1,
                                            .type = TexelType::Uint8,
                                            .texels = std::as_bytes(std::span(grey))});
        texture.Set(UsdUVTexture::kFile, std::string("memory/w.grey"));
        const Float3 neutral = Settled(renderer, false);
        EXPECT_GT(neutral.y, 0.01f);
        EXPECT_NEAR(neutral.x, neutral.z, 0.05f * neutral.y);

        EXPECT_THROW(
            scene->AddTexture(
                "memory/short",
                {.width = 4, .height = 4, .channels = 3, .texels = std::as_bytes(std::span(grey))}),
            Error);
        EXPECT_THROW(scene->AddTexture("memory/five", {.width = 1, .height = 1, .channels = 5}),
                     Error);
    });
}

TEST(PlanetaryMaterialTest, BuiltinSurfacesLinkOnceAndNeverAgain) {
    ForEachDevice([](Device& device) {
        const test::CapturedLog log(LogLevel::Debug);
        const Ref<Scene> scene = LitQuad(device);
        const Prim material = scene->Define<Material>("/Looks/Paint");
        const Prim surface = scene->Define<Shader>("/Looks/Paint/Surface", {.id = "HapkeSurface"});
        material.Connect("surface", surface, "surface");
        Renderer renderer(scene);
        EXPECT_GT(Center(renderer).y, 0.01f);
        const std::vector<float> rough(4 * 4, 0.5f);
        scene->AddTexture(
            "memory/rough",
            {.width = 4, .height = 4, .channels = 1, .texels = std::as_bytes(std::span(rough))});
        const Prim texture = scene->Define<Shader>("/Looks/Paint/Rough", {.id = "UsdUVTexture"});
        texture.Set(UsdUVTexture::kFile, std::string("memory/rough"));
        surface.Connect("thetaBar", texture, "r");
        EXPECT_GT(Center(renderer).y, 0.01f);
        for (const char* id :
             {"LommelSeeligerSurface", "LunarLambertSurface", "OrenNayarSurface"}) {
            surface.Set(Shader::kId, std::string(id));
            EXPECT_GT(Center(renderer).y, 0.01f) << id;
        }
        size_t relinks = 0;
        for (const auto& [level, message] : log.Messages()) {
            std::smatch count;
            if (std::regex_search(message, count,
                                  std::regex("(\\d+) generated material definition"))) {
                EXPECT_EQ(count[1].str(), "0") << message;
            }
            relinks += message.starts_with("relinking the render kernels") ? 1 : 0;
        }
        EXPECT_LE(relinks, 1u);
    });
}

TEST(PlanetaryMaterialTest, OnlyScenesWithPlanetarySurfacesLinkTheirCode) {
    const material::MaterialDefinitionSet plain;
    const material::MaterialDefinitionSet planetary({}, {}, true);
    EXPECT_NE(plain.Hash(), planetary.Hash());
    EXPECT_FALSE(plain.Contains(kDefinitionHapke));
    EXPECT_TRUE(planetary.Contains(kDefinitionOrenNayar));
    EXPECT_TRUE(plain.Contains(kDefinitionPreviewTextured));
    const auto without = material::MaterialDispatchSources(plain);
    const auto with = material::MaterialDispatchSources(planetary);
    EXPECT_EQ(without.at(material::kDispatchFile).find("HapkeDefinition"), std::string::npos);
    EXPECT_EQ(without.at(material::kDispatchFile).find("definitions.planetary"), std::string::npos);
    EXPECT_NE(with.at(material::kDispatchFile).find("HapkeDefinition"), std::string::npos);
}

}
}
