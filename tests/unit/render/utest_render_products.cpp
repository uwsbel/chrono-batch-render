// SPDX-License-Identifier: BSD-3-Clause
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/error.h"
#include "support/devices.h"

namespace chrono::batch_render {
namespace {

using device::Device;
using test::ForEachDevice;

const std::vector<std::string> kProducts = {"/Render/View"};

Ref<Scene> OneCamera(Device& device) {
    const Ref<Scene> scene = Scene::Create(device);
    scene->Define<Camera>("/Cam");
    scene->Define<Sphere>("/Ball", {.translate = {0, 0, -5}});
    scene->Define<Scope>("/Render");
    scene->Define<RenderVar>("/Render/Depth", {.sourceName = "DistanceToCamera"});
    scene->Define<RenderProduct>(
        "/Render/View",
        {.camera = {"/Cam"}, .resolution = {4, 3}, .orderedVars = {"/Render/Depth"}});
    return scene;
}

struct Stepped {
    uint64_t address = 0;
    std::vector<int64_t> shape;
};

Stepped Step(Renderer& renderer) {
    RenderOutput output = renderer.Step(kProducts);
    output.Wait();
    const device::Tensor& tensor =
        output["/Render/View"].frames.at(0).Source("DistanceToCamera").tensor;
    return {tensor.Address(), {tensor.Shape().begin(), tensor.Shape().end()}};
}

TEST(ProductsTest, EditsOutsideAProductKeepItsOutputs) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        Renderer renderer(scene);
        const uint64_t first = Step(renderer).address;
        scene->Define<Sphere>("/Other", {.translate = {0, 2, -5}});
        scene->Find("/Ball")->Set(Xformable::kTranslate, Double3{0, 0, -6});
        EXPECT_EQ(Step(renderer).address, first);
    });
}

TEST(ProductsTest, KernelSettingsChangeWithoutPreparingTheProductAgain) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        Renderer renderer(scene);
        const uint64_t first = Step(renderer).address;
        scene->Find("/Render/View")->Set(RealTimeAPI::kMaxBounces, 3);
        scene->Find("/Render/View")->Set(BackgroundAPI::kBackgroundColor, Float3{1, 0, 0});
        EXPECT_EQ(Step(renderer).address, first);
    });
}

TEST(ProductsTest, EditingAProductOrItsSensorPreparesItAgain) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        Renderer renderer(scene);
        EXPECT_EQ(Step(renderer).shape, (std::vector<int64_t>{1, 3, 4}));
        scene->Find("/Render/View")->Set(RenderProduct::kResolution, Uint2{2, 2});
        EXPECT_EQ(Step(renderer).shape, (std::vector<int64_t>{1, 2, 2}));
        scene->Define<Camera>("/Second");
        scene->Find("/Render/View")
            ->Set(RenderProduct::kCamera, std::vector<std::string>{"/Cam", "/Second"});
        EXPECT_EQ(Step(renderer).shape, (std::vector<int64_t>{2, 2, 2}));
    });
}

TEST(ProductsTest, ABrokenProductFailsOnlyWhenSteppedAndRecoversOnceFixed) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        scene->Define<RenderProduct>("/Render/Broken", {.orderedVars = {"/Render/Depth"}});
        Renderer renderer(scene);
        Step(renderer);
        EXPECT_THROW(renderer.Step("/Render/Broken"), Error);
        scene->Find("/Render/Broken")
            ->Set(RenderProduct::kCamera, std::vector<std::string>{"/Cam"});
        RenderOutput output = renderer.Step("/Render/Broken");
        output.Wait();
        EXPECT_EQ(output["/Render/Broken"].frames.size(), 1u);
    });
}

TEST(ProductsTest, SensorsCaptureAtTheirFrameRateOnTheScenesClock) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        scene->Find("/Cam")->Set(SensorAPI::kSensorFrameRate, 10.0f);
        Renderer renderer(scene);
        const auto captured = [&] {
            scene->Advance(0.04);
            RenderOutput output = renderer.Step(kProducts);
            output.Wait();
            return !output["/Render/View"].frames.empty();
        };
        std::vector<bool> pattern;
        for (int step = 0; step < 6; ++step) {
            pattern.push_back(captured());
        }
        EXPECT_EQ(pattern, (std::vector<bool>{true, false, true, false, true, false}));
        EXPECT_NEAR(scene->Time(), 0.24, 1e-9);

        scene->SetTime(0);
        EXPECT_TRUE(captured());
        EXPECT_FALSE(captured());
    });
}

bool Captured(Renderer& renderer) {
    RenderOutput output = renderer.Step(kProducts);
    output.Wait();
    return !output["/Render/View"].frames.empty();
}

const std::vector<bool> kTenHertzAt25Hz = {true, false, true, false, true, false};

TEST(ProductsTest, StepsWithoutTimePassingRenderEveryProduct) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        scene->Find("/Cam")->Set(SensorAPI::kSensorFrameRate, 10.0f);
        Renderer renderer(scene);
        std::vector<bool> pattern;
        for (int step = 0; step < 6; ++step) {
            scene->Advance(0.04);
            pattern.push_back(Captured(renderer));
            EXPECT_TRUE(Captured(renderer));
        }
        EXPECT_EQ(pattern, kTenHertzAt25Hz);
    });
}

TEST(ProductsTest, RenderersOnOneSceneShareItsClock) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        scene->Find("/Cam")->Set(SensorAPI::kSensorFrameRate, 10.0f);
        Renderer first(scene);
        Renderer second(scene);
        std::vector<bool> firstPattern;
        std::vector<bool> secondPattern;
        for (int step = 0; step < 6; ++step) {
            scene->Advance(0.04);
            firstPattern.push_back(Captured(first));
            secondPattern.push_back(Captured(second));
        }
        EXPECT_EQ(firstPattern, kTenHertzAt25Hz);
        EXPECT_EQ(secondPattern, kTenHertzAt25Hz);
        EXPECT_NEAR(scene->Time(), 0.24, 1e-9);
    });
}

TEST(ProductsTest, OutputsCoverTheTimeSinceTheRenderersLastStep) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        scene->Advance(1);
        Renderer renderer(scene);
        for (int advance = 0; advance < 3; ++advance) {
            scene->Advance(0.25);
        }
        RenderOutput output = renderer.Step(kProducts);
        output.Wait();
        EXPECT_DOUBLE_EQ(output.simulationStartTime, 1);
        EXPECT_DOUBLE_EQ(output.simulationEndTime, 1.75);

        output = renderer.Step(kProducts);
        output.Wait();
        EXPECT_DOUBLE_EQ(output.simulationStartTime, 1.75);
        EXPECT_DOUBLE_EQ(output.simulationEndTime, 1.75);

        scene->SetTime(3);
        scene->Advance(0.5);
        output = renderer.Step(kProducts);
        output.Wait();
        EXPECT_DOUBLE_EQ(output.simulationStartTime, 3);
        EXPECT_DOUBLE_EQ(output.simulationEndTime, 3.5);
    });
}

TEST(ProductsTest, TimeOnlyMovesForward) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = OneCamera(device);
        EXPECT_THROW(scene->Advance(-0.1), Error);
        EXPECT_THROW(scene->Advance(std::numeric_limits<double>::infinity()), Error);
        EXPECT_THROW(scene->SetTime(std::numeric_limits<double>::quiet_NaN()), Error);
        EXPECT_DOUBLE_EQ(scene->Time(), 0);
    });
}

}
}
