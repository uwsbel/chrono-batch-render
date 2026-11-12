// SPDX-License-Identifier: BSD-3-Clause
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "support/devices.h"
#include "support/world.h"

namespace chrono::batch_render {
namespace {

using device::Device;
using test::ForEachDevice;

struct Placed {
    Double3 position;
    Double3 x;
    Double3 y;
    Double3 z;
};

Placed World(Scene& scene, const std::string& path, double dt = 0) {
    scene.Advance(dt);
    const WorldTransform world = test::HierarchyTransforms(scene, {path}).front();
    const auto widen = [](Float3 v) { return Double3{v.x, v.y, v.z}; };
    return {{double(world.high.x) + world.low.x, double(world.high.y) + world.low.y,
             double(world.high.z) + world.low.z},
            widen(world.x),
            widen(world.y),
            widen(world.z)};
}

void ExpectNear(Double3 actual, Double3 expected, double tolerance = 1e-4) {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

Ref<Scene> Car(Device& device) {
    const Ref<Scene> scene = Scene::Create(device);
    scene->Define<Xform>("/Car", {.translate = {10, 0, 0}, .orient = FromAxisAngle({0, 0, 1}, 90)});
    return scene;
}

TEST(FollowTest, FollowsTheTargetsWorldRigidlyAsItMoves) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Define<Xform>("/Parent", {.translate = {0, 0, 50}});
        scene->Define<Xform>(
            "/Parent/Rider",
            {.translate = {1, 0, 0}, .follow = FollowAPI::Desc{.followTarget = {"/Car"}}});
        scene->Define<Xform>("/Parent/Rider/Seat", {.translate = {0, 0, 1}});
        ExpectNear(World(*scene, "/Parent/Rider").position, {10, 1, 0});
        ExpectNear(World(*scene, "/Parent/Rider/Seat").position, {10, 1, 1});
        scene->Find("/Car")->Set(Xformable::kTranslate, Double3{20, 0, 0});
        ExpectNear(World(*scene, "/Parent/Rider").position, {20, 1, 0});
        ExpectNear(World(*scene, "/Parent/Rider/Seat").position, {20, 1, 1});
        scene->Unapply<FollowAPI>(*scene->Find("/Parent/Rider"));
        ExpectNear(World(*scene, "/Parent/Rider/Seat").position, {1, 0, 51});
    });
}

TEST(FollowTest, TranslateAndRotateModesPickWhatTheTargetGives) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Find("/Car")->Set(Xformable::kOrient, FromEulerDegrees({30, 0, 90}));
        const auto rider = [&](const std::string& name, const FollowAPI::Desc& follow) {
            scene->Define<Xform>("/" + name, {.translate = {1, 0, 0}, .follow = follow});
            return World(*scene, "/" + name);
        };
        const Placed still =
            rider("Still", {.followTarget = {"/Car"}, .followRotate = FollowRotate::None});
        ExpectNear(still.position, {11, 0, 0});
        ExpectNear(still.x, {1, 0, 0});
        const Placed home = rider("Home", {.followTarget = {"/Car"},
                                           .followTranslate = FollowTranslate::None,
                                           .followRotate = FollowRotate::Yaw});
        ExpectNear(home.position, {0, 1, 0});
        ExpectNear(home.z, {0, 0, 1});
    });
}

TEST(FollowTest, OffsetsSitInTheTargetsFrameAndScaleWithIt) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Find("/Car")->Set(Xformable::kScale, Float3{2, 2, 2});
        scene->Define<Xform>("/Chase", {.follow = FollowAPI::Desc{
                                            .followTarget = {"/Car"},
                                            .followRotate = FollowRotate::Yaw,
                                            .followOffsetTranslate = {-6, 0, 2},
                                        }});
        ExpectNear(World(*scene, "/Chase").position, {10, -6, 2});
        scene->Find("/Chase")->Set(FollowAPI::kFollowInheritScale, true);
        const Placed scaled = World(*scene, "/Chase");
        ExpectNear(scaled.position, {10, -12, 4});
        ExpectNear(scaled.z, {0, 0, 2});
    });
}

TEST(FollowTest, LookAtAimsMinusZAtTheTargetPoint) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Define<Xform>("/Ball", {.translate = {0, 0, 0}});
        scene->Define<Camera>("/Eye", {.follow = FollowAPI::Desc{
                                           .followTarget = {"/Car"},
                                           .followLookAt = {"/Ball"},
                                           .followRotate = FollowRotate::LookAt,
                                           .followLookAtOffsetTranslate = {0, 0, 2},
                                       }});
        const Placed eye = World(*scene, "/Eye");
        ExpectNear(eye.position, {10, 0, 0});
        const double length = std::sqrt(100.0 + 4.0);
        ExpectNear(eye.z, {10 / length, 0, -2 / length});
        EXPECT_GT(eye.y.z, 0);
    });
}

TEST(FollowTest, WeightBlendsFromTheParentToTheFollowFrame) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Define<Xform>(
            "/Half", {.follow = FollowAPI::Desc{.followTarget = {"/Car"}, .followWeight = 0.5f}});
        const Placed half = World(*scene, "/Half");
        ExpectNear(half.position, {5, 0, 0});
        ExpectNear(half.x, {std::sqrt(0.5), std::sqrt(0.5), 0});
    });
}

TEST(FollowTest, PositionLagClosesExponentiallyAndStaysWithinItsMaxDistance) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Car");
        scene->Define<Xform>("/Trailer",
                             {.follow = FollowAPI::Desc{.followTarget = {"/Car"},
                                                        .followPositionLag = {0.5f, 0.5f, 0.5f}}});
        ExpectNear(World(*scene, "/Trailer", 0.5).position, {0, 0, 0});
        scene->Find("/Car")->Set(Xformable::kTranslate, Double3{10, 0, 0});
        ExpectNear(World(*scene, "/Trailer", 0.5).position, {10 * (1 - std::exp(-1.0)), 0, 0},
                   1e-3);
        ExpectNear(World(*scene, "/Trailer").position, {10 * (1 - std::exp(-1.0)), 0, 0}, 1e-3);
        ExpectNear(World(*scene, "/Trailer", 0.5).position, {10 * (1 - std::exp(-2.0)), 0, 0},
                   1e-3);

        scene->Find("/Trailer")->Set(FollowAPI::kFollowLagMaxDistance, 0.5f);
        scene->Find("/Car")->Set(Xformable::kTranslate, Double3{40, 0, 0});
        ExpectNear(World(*scene, "/Trailer", 0.5).position, {39.5, 0, 0}, 1e-3);
    });
}

TEST(FollowTest, RetargetingSnapsToTheNewTarget) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Define<Xform>("/Truck", {.translate = {0, 30, 0}});
        scene->Define<Xform>(
            "/Trailer",
            {.follow = FollowAPI::Desc{.followTarget = {"/Car"}, .followPositionLag = {5, 5, 5}}});
        ExpectNear(World(*scene, "/Trailer", 0.1).position, {10, 0, 0});
        scene->Find("/Trailer")->Set(FollowAPI::kFollowTarget, std::vector<std::string>{"/Truck"});
        ExpectNear(World(*scene, "/Trailer", 0.1).position, {0, 30, 0});
    });
}

TEST(FollowTest, PathsPlaceTheFrameAlongTheCurveFacingItsTangent) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<BasisCurves>("/Track", {.translate = {0, 0, 5},
                                              .points = {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}},
                                              .curveVertexCounts = {3},
                                              .type = CurveType::Linear});
        const Prim rider = scene->Define<Xform>(
            "/Rider",
            {.follow = FollowAPI::Desc{.followPath = {"/Track"}, .followPathPosition = 2}});
        Placed placed = World(*scene, "/Rider");
        ExpectNear(placed.position, {2, 0, 5});
        ExpectNear(placed.x, {1, 0, 0});
        ExpectNear(placed.z, {0, 0, 1});

        rider.Set(FollowAPI::kFollowPathPosition, 6.0f);
        placed = World(*scene, "/Rider");
        ExpectNear(placed.position, {4, 2, 5});
        ExpectNear(placed.x, {0, 1, 0});

        rider.Set(FollowAPI::kFollowPathPosition, 100.0f);
        ExpectNear(World(*scene, "/Rider").position, {4, 3, 5});

        rider.Set(FollowAPI::kFollowPathMode, FollowPathMode::Parameter);
        rider.Set(FollowAPI::kFollowPathPosition, 0.25f);
        ExpectNear(World(*scene, "/Rider").position, {2, 0, 5});
    });
}

TEST(FollowTest, PeriodicPathsWrapAndRidersMoveWithTheirCurve) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        const Prim loop = scene->Define<BasisCurves>(
            "/Loop", {.points = {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
                      .curveVertexCounts = {4},
                      .type = CurveType::Linear,
                      .wrap = CurveWrap::Periodic});
        scene->Define<Xform>("/Rider", {.follow = FollowAPI::Desc{.followPath = {"/Loop"},
                                                                  .followPathPosition = 9}});
        ExpectNear(World(*scene, "/Rider").position, {1, 0, 0});

        loop.Set(BasisCurves::kPoints,
                 std::vector<Float3>{{10, 0, 0}, {12, 0, 0}, {12, 2, 0}, {10, 2, 0}});
        ExpectNear(World(*scene, "/Rider").position, {11, 0, 0});

        loop.Set(BasisCurves::kTranslate, Double3{0, 0, 3});
        ExpectNear(World(*scene, "/Rider").position, {11, 0, 3});
    });
}

TEST(FollowTest, PathSpeedCarriesRidersAlongAsTheClockAdvances) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<BasisCurves>("/Track", {.points = {{0, 0, 0}, {10, 0, 0}},
                                              .curveVertexCounts = {2},
                                              .type = CurveType::Linear});
        const Prim rider = scene->Define<Xform>(
            "/Rider", {.follow = FollowAPI::Desc{.followPath = {"/Track"}, .followPathSpeed = 2}});
        ExpectNear(World(*scene, "/Rider").position, {0, 0, 0});
        ExpectNear(World(*scene, "/Rider", 1).position, {2, 0, 0});
        ExpectNear(World(*scene, "/Rider", 0.5).position, {3, 0, 0});

        rider.Set(FollowAPI::kFollowPathPosition, 5.0f);
        ExpectNear(World(*scene, "/Rider").position, {5, 0, 0});
        ExpectNear(World(*scene, "/Rider", 1).position, {7, 0, 0});

        rider.Set(FollowAPI::kFollowPathSpeed, -1.0f);
        ExpectNear(World(*scene, "/Rider", 1).position, {6, 0, 0});
        ExpectNear(World(*scene, "/Rider", 20).position, {0, 0, 0});
        rider.Set(FollowAPI::kFollowPathSpeed, 1.0f);
        ExpectNear(World(*scene, "/Rider", 1).position, {1, 0, 0});
    });
}

TEST(FollowTest, CubicPathsCarryRidersAlongTheTrueCurve) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<BasisCurves>(
            "/Bulge",
            {.points = {{3, -2, 0}, {5, -2, 0}, {5, 2, 0}, {3, 2, 0}}, .curveVertexCounts = {4}});
        scene->Define<Xform>("/Rider",
                             {.follow = FollowAPI::Desc{.followPath = {"/Bulge"},
                                                        .followPathMode = FollowPathMode::Parameter,
                                                        .followPathPosition = 0.5f}});
        const Placed placed = World(*scene, "/Rider");
        ExpectNear(placed.position, {4.5, 0, 0});
        ExpectNear(placed.x, {0, 1, 0});
    });
}

TEST(FollowTest, FollowersFollowFollowersAndCyclesFollowNothing) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = Car(device);
        scene->Define<Xform>("/Rider", {.translate = {1, 0, 0},
                                        .follow = FollowAPI::Desc{.followTarget = {"/Car"}}});
        scene->Define<Xform>("/Shadow", {.translate = {0, 0, 2},
                                         .follow = FollowAPI::Desc{.followTarget = {"/Rider"}}});
        scene->Define<Xform>("/Shadow/Hat", {.translate = {0, 0, 1}});
        ExpectNear(World(*scene, "/Shadow/Hat").position, {10, 1, 3});

        const Prim ping = scene->Define<Xform>("/Ping", {.translate = {1, 0, 0}});
        scene->Define<Xform>("/Pong", {.translate = {0, 1, 0},
                                       .follow = FollowAPI::Desc{.followTarget = {"/Ping"}}});
        scene->Apply<FollowAPI>(ping, {.followTarget = {"/Pong"}});
        ExpectNear(World(*scene, "/Ping").position, {1, 0, 0});
        ExpectNear(World(*scene, "/Pong").position, {0, 1, 0});
        ExpectNear(World(*scene, "/Shadow/Hat").position, {10, 1, 3});
    });
}

}
}
