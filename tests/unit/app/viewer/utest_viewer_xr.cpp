// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/panels/xr.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/app/viewer/viewer.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/scene/scene.h"
#include "support/threads.h"

namespace chrono::batch_render::app {
namespace {

constexpr uint32_t kWidth = 160;
constexpr uint32_t kHeight = 90;
constexpr auto kPatience = std::chrono::seconds(180);

class XrViewerTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        s_device = device::Device::Create();
    }

    static void TearDownTestSuite() {
        s_device = nullptr;
    }

    static Ref<device::Device> s_device;
};

Ref<device::Device> XrViewerTest::s_device;

Ref<Scene> Row(device::Device& device) {
    const Ref<Scene> scene = Scene::Create(device);
    scene->Define<Cube>("/Left", {.translate = {-6, 0, 1}});
    scene->Define<Cube>("/Middle", {.translate = {0, 0, 1}});
    scene->Define<Cube>("/Right", {.translate = {6, 0, 1}});
    return scene;
}

template <class Done> bool FrameUntil(Viewer& viewer, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < deadline) {
        viewer.Frame();
        if (done()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<XrPanel> AddXr(Viewer& viewer, const Ref<Scene>& scene) {
    std::shared_ptr<XrPanel> panel;
    ChronoBatchRenderAddXrPanel(&viewer, &scene, &panel);
    return panel;
}

TEST_F(XrViewerTest, OffersAnXrPanelThatDrawsUntilStarted) {
    Viewer viewer(Row(*s_device), {.width = kWidth, .height = kHeight, .offscreen = true});
    viewer.ShowPanel("XR", true);
    for (int frame = 0; frame < 4; ++frame) {
        viewer.Frame();
    }
    EXPECT_EQ(viewer.Viewports(), (std::vector<std::string>{"Viewport"}));
}

size_t Colors(const std::vector<uint8_t>& pixels, int64_t width, int64_t from, int64_t to) {
    std::set<uint32_t> colors;
    for (size_t at = 0; at + 3 < pixels.size(); at += 4) {
        const int64_t x = int64_t(at / 4) % width;
        if (x >= from && x < to) {
            colors.insert(pixels[at] | pixels[at + 1] << 8 | pixels[at + 2] << 16);
        }
    }
    return colors.size();
}

TEST_F(XrViewerTest, StartsXrAtTheViewportCameraAndMirrorsBothEyes) {
    const Ref<Scene> scene = Row(*s_device);
    Viewer viewer(scene,
                  {.width = kWidth, .height = kHeight, .offscreen = true, .builtinPanels = false});
    const std::shared_ptr<XrPanel> xr = AddXr(viewer, scene);
    viewer.ShowPanel("XR", true);
    ASSERT_TRUE(FrameUntil(viewer, [&] { return viewer.Shown() != nullptr; }));
    xr->Start();
    ASSERT_TRUE(xr->Running());
    EXPECT_EQ(viewer.Viewports(), (std::vector<std::string>{"Viewport", "XR Mirror"}));
    ASSERT_TRUE(FrameUntil(viewer, [&] { return viewer.Shown("XR Mirror") != nullptr; }));
    for (int frame = 0; frame < 10; ++frame) {
        viewer.Frame();
    }
    const Double3 root = scene->Find("/__chrono_batch_render_xr")->Get(Xformable::kTranslate);
    EXPECT_GT(std::hypot(root.x, root.y), 3.0);
    EXPECT_NEAR(root.z, 0, 1e-9);
    const device::Tensor& mirror = *viewer.Shown("XR Mirror");
    const int64_t width = mirror.Shape()[1];
    ASSERT_EQ(width % 2, 0);
    const std::vector<uint8_t> pixels = mirror.ToHost<uint8_t>();
    EXPECT_GT(Colors(pixels, width, 0, width / 2), 1u);
    EXPECT_GT(Colors(pixels, width, width / 2, width), 1u);
    xr->Stop();
    EXPECT_FALSE(xr->Running());
    viewer.Frame();
    EXPECT_EQ(viewer.Viewports(), (std::vector<std::string>{"Viewport"}));
    EXPECT_FALSE(scene->Find("/__chrono_batch_render_xr").has_value());
}

TEST_F(XrViewerTest, SpectatesFromBehindTheHead) {
    const Ref<Scene> scene = Row(*s_device);
    Viewer viewer(scene,
                  {.width = kWidth, .height = kHeight, .offscreen = true, .builtinPanels = false});
    const std::shared_ptr<XrPanel> xr = AddXr(viewer, scene);
    ASSERT_TRUE(FrameUntil(viewer, [&] { return viewer.Shown() != nullptr; }));
    xr->Spectate(true);
    xr->Start();
    ASSERT_TRUE(xr->Running());
    ASSERT_TRUE(FrameUntil(viewer, [&] {
        const device::Tensor* shown = viewer.Shown("XR Mirror");
        return shown != nullptr && shown->Shape().size() == 3;
    }));
    const device::Tensor& shown = *viewer.Shown("XR Mirror");
    EXPECT_EQ(shown.Shape()[0], 720);
    EXPECT_EQ(shown.Shape()[1], 1280);
    EXPECT_TRUE(scene->Find("/__chrono_batch_render_xr_spectator/Camera").has_value());
    xr->Stop();
    EXPECT_FALSE(scene->Find("/__chrono_batch_render_xr_spectator").has_value());
}

TEST_F(XrViewerTest, StartsXrFromTheApi) {
    Viewer viewer(Row(*s_device), {.width = kWidth, .height = kHeight, .offscreen = true});
    viewer.StartXr(true);
    ASSERT_TRUE(FrameUntil(viewer, [&] { return viewer.Shown("XR Mirror") != nullptr; }));
    EXPECT_EQ(viewer.Viewports(), (std::vector<std::string>{"Viewport", "XR Mirror"}));
}

TEST_F(XrViewerTest, XrKeepsPresentingWhileTheViewerIsIdle) {
    const Ref<Scene> scene = Row(*s_device);
    Viewer viewer(scene,
                  {.width = kWidth, .height = kHeight, .offscreen = true, .builtinPanels = false});
    const std::shared_ptr<XrPanel> xr = AddXr(viewer, scene);
    ASSERT_TRUE(FrameUntil(viewer, [&] { return viewer.Shown() != nullptr; }));
    xr->Start();
    ASSERT_TRUE(FrameUntil(viewer, [&] { return xr->FramesPresented() > 0; }));
    const uint64_t before = xr->FramesPresented();
    const auto deadline = std::chrono::steady_clock::now() + kPatience;
    while (xr->FramesPresented() < before + 10 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GE(xr->FramesPresented(), before + 10);
    xr->Stop();
    EXPECT_FALSE(xr->Running());
}

TEST_F(XrViewerTest, XrStartsAndStopsOverAndOverWhileTheSceneChanges) {
    const Ref<Scene> scene = Row(*s_device);
    Viewer viewer(scene,
                  {.width = kWidth, .height = kHeight, .offscreen = true, .builtinPanels = false});
    const std::shared_ptr<XrPanel> xr = AddXr(viewer, scene);
    ASSERT_TRUE(FrameUntil(viewer, [&] { return viewer.Shown() != nullptr; }));
    std::atomic<bool> running = true;
    test::WithinDeadline(std::chrono::seconds(300), "XR start and stop", [&] {
        std::thread writer([&] {
            const Prim cube = *scene->Find("/Middle");
            for (int move = 0; running; ++move) {
                cube.Set(Xformable::kTranslate, Double3{0, double(move % 50) * 0.02, 1});
                std::this_thread::yield();
            }
        });
        for (int round = 0; round < 12; ++round) {
            xr->Start();
            ASSERT_TRUE(xr->Running());
            const uint64_t until = round % 3 == 0 ? 0 : 3;
            ASSERT_TRUE(FrameUntil(viewer, [&] { return xr->FramesPresented() >= until; }));
            viewer.Frame();
            xr->Stop();
            ASSERT_FALSE(xr->Running());
            EXPECT_FALSE(scene->Find("/__chrono_batch_render_xr").has_value());
            viewer.Frame();
        }
        running = false;
        writer.join();
    });
    EXPECT_EQ(viewer.Viewports(), (std::vector<std::string>{"Viewport"}));
}

}
}
