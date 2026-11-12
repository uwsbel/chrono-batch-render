// SPDX-License-Identifier: BSD-3-Clause
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/xr/rig.h"
#include "chrono_batch_render/xr/session.h"

namespace chrono::batch_render::xr {
namespace {

std::unique_ptr<Session> OpenRuntime() {
    const char* wanted = std::getenv("CHRONO_BATCH_RENDER_TEST_OPENXR");
    if (wanted == nullptr || std::string_view(wanted) != "1") {
        return nullptr;
    }
    SessionDesc desc;
    desc.backend = Backend::OpenXR;
    desc.resolutionScale = 0.1f;
    try {
        return std::make_unique<Session>(desc);
    } catch (const UnsupportedError&) {
        return nullptr;
    }
}

uint64_t Present(Scene& scene, Session& session, Rig& rig, Renderer& renderer, uint32_t frames) {
    uint64_t presented = 0;
    for (uint32_t i = 0; i < frames && session.Running(); ++i) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        if (frame.shouldRender) {
            scene.Advance(frame.deltaTime);
            RenderOutput output = renderer.Step(rig.Product());
            session.EndFrame(frame, output);
            ++presented;
        } else {
            session.EndFrame(frame, RenderOutput());
        }
    }
    return presented;
}

Ref<Scene> BallAhead(device::Device& device) {
    const Ref<Scene> scene = Scene::Create(device);
    scene->Define<Sphere>("/Ball", {.translate = {0, 4, 1.6}, .radius = 1});
    return scene;
}

TEST(OpenXrTest, PresentsFramesOnTheRuntimesDevice) {
    const std::unique_ptr<Session> session = OpenRuntime();
    if (!session) {
        GTEST_SKIP() << "set CHRONO_BATCH_RENDER_TEST_OPENXR=1 with an OpenXR runtime running";
    }
    const Ref<device::Device> device = session->CreateDevice();
    EXPECT_EQ(device->Adapter().backend, device::Backend::Vulkan);
    const Ref<Scene> scene = BallAhead(*device);
    Rig rig(scene, *session);
    Renderer renderer(scene);
    EXPECT_GT(Present(*scene, *session, rig, renderer, 30), 0u);
    const SessionStatus status = session->Status();
    EXPECT_EQ(status.backend, Backend::OpenXR);
    EXPECT_FALSE(status.runtime.empty());
    EXPECT_GT(status.framesPresented, 0u);
    session->RequestExit();
    for (uint32_t i = 0; i < 10 && session->Running(); ++i) {
        session->EndFrame(session->BeginFrame(), RenderOutput());
    }
}

TEST(OpenXrTest, StagesEyesRenderedOnAnotherDevice) {
    const std::unique_ptr<Session> session = OpenRuntime();
    if (!session) {
        GTEST_SKIP() << "set CHRONO_BATCH_RENDER_TEST_OPENXR=1 with an OpenXR runtime running";
    }
    const Ref<device::Device> headset = session->CreateDevice();
    const Ref<device::Device> render =
        device::Device::Create({.adapter = headset->Adapter().alias});
    ASSERT_NE(render.Get(), headset.Get());
    const Ref<Scene> scene = BallAhead(*render);
    Rig rig(scene, *session);
    Renderer renderer(scene);
    EXPECT_GT(Present(*scene, *session, rig, renderer, 30), 0u);
    EXPECT_GT(session->Status().framesPresented, 0u);
}

}
}
