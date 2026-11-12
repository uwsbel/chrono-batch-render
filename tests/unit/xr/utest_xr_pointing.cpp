// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/xr/poses.h"
#include "chrono_batch_render/xr/rig.h"
#include "chrono_batch_render/xr/session.h"
#include "chrono_batch_render/xr/ui.h"
#include "xr_device.h"

namespace chrono::batch_render::xr {
namespace {

using test::RenderDevice;

SessionDesc Simulated(uint64_t pressFrom, uint64_t pressTo) {
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.profile.resolution = {64, 64};
    desc.simulated.placeControllers = true;
    desc.simulated.script = [=](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = {0, 0, 1.6};
        if (frame >= pressFrom && frame < pressTo) {
            input.Press(Side::Right, "trigger/value", 1);
        }
    };
    return desc;
}

std::vector<PointerGesture> Gestures(const std::vector<PointerEvent>& events) {
    std::vector<PointerGesture> gestures;
    for (const PointerEvent& event : events) {
        gestures.push_back(event.gesture);
    }
    return gestures;
}

TEST(PointingTest, HoversAndClicksTheSceneWithTheTrigger) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    Session session(Simulated(2, 4));
    const Ref<Scene> scene = Scene::Create(*device);
    const Prim ball = scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
    Rig rig(scene, session);
    Renderer renderer(scene);
    Pointing pointing;
    std::vector<PointerEvent> events;
    for (int k = 0; k < 6; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        for (const PointerEvent& event : pointing.Update(rig, *renderer.GetOverlays(), frame)) {
            events.push_back(event);
        }
    }
    EXPECT_EQ(Gestures(events),
              (std::vector<PointerGesture>{PointerGesture::Hover, PointerGesture::Press,
                                           PointerGesture::Release, PointerGesture::Click}));
    for (const PointerEvent& event : events) {
        EXPECT_EQ(event.target.prim, ball.Index());
        EXPECT_FALSE(event.target.overlay.has_value());
    }
    ASSERT_TRUE(pointing.Target().has_value());
    EXPECT_NEAR(pointing.Target()->position.y, 4, 1e-3);
}

TEST(PointingTest, OverlayItemsNearerThanTheSceneTakeTheLaser) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    Session session(Simulated(1000, 1000));
    const Ref<Scene> scene = Scene::Create(*device);
    scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
    Rig rig(scene, session);
    Renderer renderer(scene);
    rig.Update(session.BeginFrame());
    const PointerHit laser = rig.Laser(Side::Right);
    ASSERT_TRUE(laser.hit);
    const Double3 from = rig.WorldPose({laser.path.front()}).position;
    const Double3 to = rig.WorldPose({laser.localPosition}).position;
    const Double3 middle{(from.x + to.x) / 2, (from.y + to.y) / 2, (from.z + to.z) / 2};
    Overlay button;
    OverlayStyle style;
    style.size = 24;
    style.id = 7;
    button.Marker({float(middle.x), float(middle.y), float(middle.z)}, style);
    renderer.GetOverlays()->Set("ui", std::move(button));

    Pointing pointing;
    bool found = false;
    for (int k = 0; k < 12 && !found; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        pointing.Update(rig, *renderer.GetOverlays(), frame);
        scene->Advance(frame.deltaTime);
        RenderOutput output = renderer.Step(rig.Product());
        output.Done().Wait();
        session.EndFrame(frame, output);
        found = pointing.Target() && pointing.Target()->overlay;
    }
    ASSERT_TRUE(found);
    const PointerTarget& target = *pointing.Target();
    EXPECT_EQ(target.overlay->overlay, "ui");
    EXPECT_EQ(target.overlay->id, 7u);
    const double half = std::hypot(to.x - from.x, to.y - from.y, to.z - from.z) / 2;
    EXPECT_NEAR(target.distance, half, 0.05 + 0.02 * half);
}

void RunPanel(uint64_t pressFrom, uint64_t pressTo, int frames,
              const std::function<void(SpatialUi&, int)>& widgets) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    Session session(Simulated(pressFrom, pressTo));
    const Ref<Scene> scene = Scene::Create(*device);
    scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
    Rig rig(scene, session);
    Renderer renderer(scene);
    rig.Update(session.BeginFrame());
    const PointerHit laser = rig.Laser(Side::Right);
    ASSERT_TRUE(laser.hit);
    const Double3 from = rig.WorldPose({laser.path.front()}).position;
    const Double3 to = rig.WorldPose({laser.localPosition}).position;
    const PanelDesc probe;
    const double row = 0.006 + probe.rowHeight / 2;
    const Double3 top{(from.x + to.x) / 2, (from.y + to.y) / 2, (from.z + to.z) / 2 + row};
    const PanelDesc desc{.name = "test", .pose = Pose{top, {0, 0, 0, 1}, true}};
    Pointing pointing;
    SpatialUi ui;
    for (int k = 0; k < frames; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        const std::vector<PointerEvent> events =
            pointing.Update(rig, *renderer.GetOverlays(), frame);
        ui.Begin(rig, frame, pointing, events);
        if (ui.BeginPanel(desc)) {
            widgets(ui, k);
        }
        ui.EndPanel();
        ui.End(*renderer.GetOverlays());
        scene->Advance(frame.deltaTime);
        RenderOutput output = renderer.Step(rig.Product());
        output.Done().Wait();
        session.EndFrame(frame, output);
    }
}

TEST(SpatialUiTest, ButtonsHoverThenClickWithTheTrigger) {
    int clicks = 0;
    int clickedAt = -1;
    bool hoveredBefore = false;
    RunPanel(10, 12, 16, [&](SpatialUi& ui, int frame) {
        if (frame < 10 && ui.Hovered()) {
            hoveredBefore = true;
        }
        if (ui.Button("Go")) {
            ++clicks;
            clickedAt = frame;
        }
    });
    EXPECT_TRUE(hoveredBefore);
    EXPECT_EQ(clicks, 1);
    EXPECT_EQ(clickedAt, 11);
}

TEST(SpatialUiTest, SlidersFollowTheLaserWhileHeld) {
    float value = 0;
    RunPanel(10, 14, 16, [&](SpatialUi& ui, int) { ui.Slider("Speed", value, 0, 10); });
    EXPECT_NEAR(value, 5, 0.5);
}

TEST(GrabbingTest, TheGripCarriesWhatTheLaserIsOnAndReportsTheMove) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.profile.resolution = {64, 64};
    desc.simulated.placeControllers = true;
    desc.simulated.script = [](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = {0, 0, 1.6};
        if (frame >= 5) {
            Controller& right = input.controllers[uint32_t(Side::Right)];
            right.pose.position.x += 1;
            right.aim.position.x += 1;
        }
        if (frame >= 2 && frame < 8) {
            input.Press(Side::Right, "squeeze/value", 1);
        }
    };
    Session session(desc);
    const Ref<Scene> scene = Scene::Create(*device);
    const Prim ball = scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
    Rig rig(scene, session);
    Renderer renderer(scene);
    Pointing pointing;
    Grabbing grabbing;
    std::optional<Grabbing::Moved> moved;
    bool held = false;
    for (int k = 0; k < 10; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        pointing.Update(rig, *renderer.GetOverlays(), frame);
        if (std::optional<Grabbing::Moved> done = grabbing.Update(rig, frame, pointing)) {
            moved = done;
        }
        held = held || grabbing.Held().has_value();
    }
    EXPECT_TRUE(held);
    EXPECT_FALSE(grabbing.Held().has_value());
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->path, "/Ball");
    EXPECT_NEAR(moved->translateBefore.x, 0.2, 1e-6);
    EXPECT_NEAR(moved->translate.x, 1.2, 1e-3);
    EXPECT_NEAR(moved->translate.y, 5, 1e-3);
    EXPECT_NEAR(ball.Get(Xformable::kTranslate).x, 1.2, 1e-3);
}

TEST(PointingTest, BuzzesOnHoversAndPressesOfSomething) {
    Frame frame;
    PointerTarget ball;
    ball.prim = 3;
    const std::vector<PointerEvent> events = {
        {.gesture = PointerGesture::Hover, .hand = Side::Left, .target = ball},
        {.gesture = PointerGesture::Leave, .hand = Side::Left, .target = ball},
        {.gesture = PointerGesture::Hover, .hand = Side::Left, .target = {}},
        {.gesture = PointerGesture::Press, .hand = Side::Left, .target = {}},
    };
    Buzz(frame, events, {.hover = 0.1f, .press = 0.7f, .duration = 0.02});
    ASSERT_EQ(frame.haptics.size(), 2u);
    EXPECT_EQ(frame.haptics[0].hand, Side::Left);
    EXPECT_FLOAT_EQ(frame.haptics[0].amplitude, 0.1f);
    EXPECT_FLOAT_EQ(frame.haptics[1].amplitude, 0.7f);
    EXPECT_DOUBLE_EQ(frame.haptics[1].duration, 0.02);
}

TEST(SpatialUiTest, FingertipsPushButtonsOnce) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    const Double3 head{0, 0, 1.6};
    const Double3 start{-0.2, 0.3, 0.6 * 1.6};
    Double3 toward{head.x - start.x, head.y - start.y, 0};
    const double length = std::hypot(toward.x, toward.y);
    toward = {toward.x / length, toward.y / length, 0};
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.profile.resolution = {64, 64};
    desc.simulated.placeControllers = true;
    desc.simulated.script = [=](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = head;
        Controller& left = input.controllers[uint32_t(Side::Left)];
        left.pose.position = start;
        if (frame < 4) {
            left.pose.position = {start.x + 0.05 * toward.x, start.y + 0.05 * toward.y, start.z};
        }
        left.aim = left.pose;
    };
    Session session(desc);
    const Ref<Scene> scene = Scene::Create(*device);
    Rig rig(scene, session);
    Renderer renderer(scene);
    const PanelDesc probe;
    const double row = 0.006 + probe.rowHeight / 2;
    const PanelDesc panel{.name = "touch",
                          .pose = Pose{{start.x, start.y, start.z + row}, {0, 0, 0, 1}, true}};
    Pointing pointing;
    SpatialUi ui;
    int presses = 0;
    bool poked = false;
    for (int k = 0; k < 8; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        const std::vector<PointerEvent> events =
            pointing.Update(rig, *renderer.GetOverlays(), frame);
        ui.Begin(rig, frame, pointing, events);
        if (ui.BeginPanel(panel) && ui.Button("Push")) {
            ++presses;
        }
        ui.EndPanel();
        poked = poked || !ui.Pokes().empty();
        ui.End(*renderer.GetOverlays());
        scene->Advance(frame.deltaTime);
        session.EndFrame(frame, renderer.Step(rig.Product()));
    }
    EXPECT_EQ(presses, 1);
    EXPECT_TRUE(poked);
}

TEST(SpatialUiTest, RendersFlatTextIntoAnImage) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    const device::Tensor image = RenderOverlayText(*device, "Speed 1.50", 32, {1, 1, 1, 1});
    ASSERT_EQ(image.Shape().size(), 3u);
    EXPECT_GE(image.Shape()[0], 20);
    EXPECT_LE(image.Shape()[0], 32);
    EXPECT_GT(image.Shape()[1], image.Shape()[0] * 3);
    EXPECT_EQ(image.Shape()[2], 4);
    const std::vector<uint8_t> pixels = image.ToHost<uint8_t>();
    size_t inked = 0;
    for (size_t at = 3; at < pixels.size(); at += 4) {
        inked += pixels[at] > 128 ? 1 : 0;
    }
    EXPECT_GT(inked, pixels.size() / 4 / 20);
    EXPECT_LT(inked, pixels.size() / 4 / 2);
}

TEST(GrabbingTest, BothGripsScaleAboutTheMidpointBetweenTheHands) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.profile.resolution = {64, 64};
    desc.simulated.placeControllers = true;
    desc.simulated.script = [](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = {0, 0, 1.6};
        if (frame >= 2 && frame < 12) {
            input.Press(Side::Right, "squeeze/value", 1);
        }
        if (frame >= 4 && frame < 9) {
            input.Press(Side::Left, "squeeze/value", 1);
        }
        if (frame >= 6) {
            Controller& left = input.controllers[uint32_t(Side::Left)];
            left.pose.position.x -= 0.4;
            left.aim.position.x -= 0.4;
        }
    };
    Session session(desc);
    const Ref<Scene> scene = Scene::Create(*device);
    const Prim ball = scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
    Rig rig(scene, session);
    Renderer renderer(scene);
    Pointing pointing;
    Grabbing grabbing;
    std::optional<Grabbing::Moved> moved;
    bool both = false;
    for (int k = 0; k < 14; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        pointing.Update(rig, *renderer.GetOverlays(), frame);
        if (std::optional<Grabbing::Moved> done = grabbing.Update(rig, frame, pointing)) {
            moved = done;
        }
        both = both || grabbing.TwoHanded();
    }
    EXPECT_TRUE(both);
    ASSERT_TRUE(moved.has_value());
    EXPECT_NEAR(moved->scaleBefore.x, 1, 1e-6);
    EXPECT_NEAR(moved->scale.x, 2, 1e-3);
    EXPECT_NEAR(moved->scale.z, 2, 1e-3);
    EXPECT_NEAR(moved->translate.x, 0.2, 1e-3);
    EXPECT_NEAR(moved->translate.y, 9.7, 1e-3);
    EXPECT_NEAR(moved->translate.z, 0.96, 1e-3);
    EXPECT_NEAR(ball.Get(Xformable::kScale).y, 2, 1e-3);
}

std::optional<Grabbing::Moved> Carry(Scene& scene, Session& session, const GrabSnapping& snap) {
    Rig rig(Ref<Scene>(&scene), session);
    Renderer renderer{Ref<Scene>(&scene)};
    Pointing pointing;
    Grabbing grabbing;
    grabbing.SetSnapping(snap);
    std::optional<Grabbing::Moved> moved;
    for (int k = 0; k < 10; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        pointing.Update(rig, *renderer.GetOverlays(), frame);
        if (std::optional<Grabbing::Moved> done = grabbing.Update(rig, frame, pointing)) {
            moved = done;
        }
    }
    return moved;
}

SessionDesc Carrying() {
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.profile.resolution = {64, 64};
    desc.simulated.placeControllers = true;
    desc.simulated.script = [](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = {0, 0, 1.6};
        if (frame >= 5) {
            Controller& right = input.controllers[uint32_t(Side::Right)];
            right.pose.position.x += 1;
            right.aim.position.x += 1;
        }
        if (frame >= 2 && frame < 8) {
            input.Press(Side::Right, "squeeze/value", 1);
        }
    };
    return desc;
}

TEST(GrabbingTest, MovesPrimsUnderAScopeInTheirOwnFrame) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    Session session(Carrying());
    const Ref<Scene> scene = Scene::Create(*device);
    scene->Define<Xform>("/World", {.translate = {10, 0, 0}});
    scene->Define<Scope>("/World/Group");
    const Prim ball =
        scene->Define<Sphere>("/World/Group/Ball", {.translate = {-9.8, 5, 0.96}, .radius = 1});
    const std::optional<Grabbing::Moved> moved = Carry(*scene, session, {});
    ASSERT_TRUE(moved.has_value());
    EXPECT_NEAR(moved->translate.x, -8.8, 1e-3);
    EXPECT_NEAR(ball.Get(Xformable::kTranslate).y, 5, 1e-3);
}

TEST(GrabbingTest, SnapsToTheGridAndSetsDownOnSurfaces) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    {
        Session session(Carrying());
        const Ref<Scene> scene = Scene::Create(*device);
        scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
        const std::optional<Grabbing::Moved> moved = Carry(*scene, session, {.grid = 0.5});
        ASSERT_TRUE(moved.has_value());
        EXPECT_NEAR(moved->translate.x, 1.0, 1e-6);
        EXPECT_NEAR(moved->translate.y, 5.0, 1e-6);
        EXPECT_NEAR(moved->translate.z, 1.0, 1e-6);
    }
    {
        Session session(Carrying());
        const Ref<Scene> scene = Scene::Create(*device);
        scene->Define<Plane>("/Floor", {.width = 0, .length = 0});
        scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
        const std::optional<Grabbing::Moved> moved = Carry(*scene, session, {.surface = 3});
        ASSERT_TRUE(moved.has_value());
        EXPECT_NEAR(moved->translate.z, 1.0, 1e-2);
    }
}

TEST(GrabbingTest, LeavesFollowersAlone) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    Session session(Carrying());
    const Ref<Scene> scene = Scene::Create(*device);
    scene->Define<Xform>("/Leader");
    const Prim ball = scene->Define<Sphere>("/Ball", {.translate = {0.2, 5, 0.96}, .radius = 1});
    scene->Apply<FollowAPI>(Prim(ball));
    EXPECT_FALSE(Carry(*scene, session, {}).has_value());
}

}

TEST(SpatialUiTest, TheLaserClicksAWristPanel) {
    device::Device* device = RenderDevice();
    if (device == nullptr) {
        GTEST_SKIP() << "no supported adapter";
    }
    const auto dot = [](Double3 a, Double3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto cross = [](Double3 a, Double3 b) {
        return Double3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    const Double3 head{0, 0, 1.6};
    const Double3 left{-0.15, 0.45, 1.35};
    const Double3 right{0.2, 0.35, 1.3};
    Double3 normal{head.x - left.x, head.y - left.y, head.z - left.z};
    normal = Scale(normal, 1 / std::sqrt(dot(normal, normal)));
    Double3 up = Add(Double3{0, 0, 1}, Scale(normal, -normal.z));
    up = Scale(up, 1 / std::sqrt(dot(up, up)));
    const PanelDesc probe;
    const double rows = 0.006 + probe.rowHeight + 0.006;
    const Double3 button = Add(left, Scale(up, 0.05 + rows - 0.006 - probe.rowHeight / 2));
    Double3 aim{button.x - right.x, button.y - right.y, button.z - right.z};
    aim = Scale(aim, 1 / std::sqrt(dot(aim, aim)));
    const Double3 back = Scale(aim, -1);
    Double3 side = cross(Double3{0, 0, 1}, back);
    side = Scale(side, 1 / std::sqrt(dot(side, side)));
    const Quatd pointed = FromAxes(side, cross(back, side), back);
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.profile.resolution = {256, 256};
    desc.simulated.placeControllers = true;
    desc.simulated.script = [=](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = head;
        Controller& hand = input.controllers[uint32_t(Side::Left)];
        hand.pose = {left, {0, 0, 0, 1}, true};
        hand.aim = hand.pose;
        Controller& pointer = input.controllers[uint32_t(Side::Right)];
        pointer.pose = {right, pointed, true};
        pointer.aim = pointer.pose;
        if (frame >= 10 && frame < 12) {
            input.Press(Side::Right, "trigger/value", 1);
        }
    };
    Session session(desc);
    const Ref<Scene> scene = Scene::Create(*device);
    Rig rig(scene, session);
    Renderer renderer(scene);
    Pointing pointing;
    SpatialUi ui;
    int clicks = 0;
    bool hovered = false;
    for (int k = 0; k < 16; ++k) {
        const Frame frame = session.BeginFrame();
        rig.Update(frame);
        const std::vector<PointerEvent> events =
            pointing.Update(rig, *renderer.GetOverlays(), frame);
        ui.Begin(rig, frame, pointing, events);
        if (ui.BeginPanel({.name = "wrist", .mount = PanelMount::Hand, .hand = Side::Left}) &&
            ui.Button("Go")) {
            ++clicks;
        }
        ui.EndPanel();
        hovered = hovered || ui.Hovered();
        ui.End(*renderer.GetOverlays());
        scene->Advance(frame.deltaTime);
        RenderOutput output = renderer.Step(rig.Product());
        output.Done().Wait();
        session.EndFrame(frame, output);
    }
    EXPECT_TRUE(hovered);
    EXPECT_EQ(clicks, 1);
}
}
