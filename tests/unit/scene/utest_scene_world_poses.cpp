// SPDX-License-Identifier: BSD-3-Clause
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/error.h"
#include "support/devices.h"

namespace chrono::batch_render {
namespace {

using test::ForEachDevice;

template <class T> DLTensor HostTensor(std::vector<T>& values, std::vector<int64_t>& shape) {
    DLTensor tensor{};
    tensor.data = values.data();
    tensor.device = {kDLCPU, 0};
    tensor.ndim = int32_t(shape.size());
    tensor.dtype = {kDLFloat, uint8_t(sizeof(T) * 8), 1};
    tensor.shape = shape.data();
    return tensor;
}

void ExpectPose(const std::vector<double>& poses, size_t prim, Double3 at, Quatd turn,
                double tolerance = 1e-5) {
    const double* pose = poses.data() + prim * 7;
    EXPECT_NEAR(pose[0], at.x, tolerance) << "prim " << prim;
    EXPECT_NEAR(pose[1], at.y, tolerance) << "prim " << prim;
    EXPECT_NEAR(pose[2], at.z, tolerance) << "prim " << prim;
    const double sign =
        pose[3] * turn.x + pose[4] * turn.y + pose[5] * turn.z + pose[6] * turn.w < 0 ? -1 : 1;
    EXPECT_NEAR(sign * pose[3], turn.x, 1e-5) << "prim " << prim;
    EXPECT_NEAR(sign * pose[4], turn.y, 1e-5) << "prim " << prim;
    EXPECT_NEAR(sign * pose[5], turn.z, 1e-5) << "prim " << prim;
    EXPECT_NEAR(sign * pose[6], turn.w, 1e-5) << "prim " << prim;
}

std::vector<double> WorldPoses(const View& view) {
    return view.ReadWorldPoses(device::DType::Float64).ToHost<double>();
}

TEST(WorldPosesTest, ReadsComposeTheHierarchy) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        const Quatd quarter = FromAxisAngle({0, 0, 1}, 90);
        scene->Define<Xform>("/Car", {.translate = {10, 0, 0}, .orient = quarter});
        scene->Define<Sphere>("/Car/Wheel", {.translate = {1, 0, 0}});
        scene->Define<Sphere>("/Ball", {.translate = {0, 0, 5}});
        const View view = scene->Select(std::vector<std::string>{"/Car", "/Car/Wheel", "/Ball"});
        const std::vector<double> poses = WorldPoses(view);
        ExpectPose(poses, 0, {10, 0, 0}, quarter);
        ExpectPose(poses, 1, {10, 1, 0}, quarter);
        ExpectPose(poses, 2, {0, 0, 5}, {0, 0, 0, 1});
        const std::vector<float> narrow = view.ReadWorldPoses().ToHost<float>();
        EXPECT_NEAR(narrow[7 + 1], 1.0f, 1e-5f);
    });
}

TEST(WorldPosesTest, ReadsSeeWritesWithoutAPublish) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Parent", {.translate = {0, 0, 1}});
        const Prim child = scene->Define<Sphere>("/Parent/Child", {.translate = {0, 2, 0}});
        const View view = scene->Select(std::vector<std::string>{"/Parent/Child"});
        ExpectPose(WorldPoses(view), 0, {0, 2, 1}, {0, 0, 0, 1});
        scene->Find("/Parent")->Set(Xformable::kTranslate, Double3{5, 0, 0});
        ExpectPose(WorldPoses(view), 0, {5, 2, 0}, {0, 0, 0, 1});
        child.Set(Xformable::kTranslate, Double3{0, 0, 3});
        ExpectPose(WorldPoses(view), 0, {5, 0, 3}, {0, 0, 0, 1});
    });
}

TEST(WorldPosesTest, RefreshingForPosesLeavesTheRenderTablesTheirChanges) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Parent");
        scene->Define<Sphere>("/Parent/Ball", {.radius = 1});
        scene->Publish({.wait = true}).Wait();
        const View view = scene->Select(std::vector<std::string>{"/Parent/Ball"});
        scene->Find("/Parent")->Set(Xformable::kTranslate, Double3{10, 0, 0});
        ExpectPose(WorldPoses(view), 0, {10, 0, 0}, {0, 0, 0, 1});
        const std::optional<Box> moved = scene->Bounds();
        ASSERT_TRUE(moved.has_value());
        EXPECT_NEAR(moved->lower.x, 9, 1e-5);
        EXPECT_NEAR(moved->upper.x, 11, 1e-5);
        std::vector<double> target = {0, 20, 0, 0, 0, 0, 1};
        std::vector<int64_t> shape = {1, 7};
        view.WriteWorldPoses(HostTensor(target, shape));
        ExpectPose(WorldPoses(view), 0, {0, 20, 0}, {0, 0, 0, 1});
        const std::optional<Box> written = scene->Bounds();
        ASSERT_TRUE(written.has_value());
        EXPECT_NEAR(written->lower.y, 19, 1e-5);
        EXPECT_NEAR(written->upper.x, 1, 1e-5);
    });
}

TEST(WorldPosesTest, LargeWorldsReadBackInFullPrecision) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Planet", {.translate = {6371000.125, -1e7 - 0.25, 0}});
        scene->Define<Sphere>("/Planet/Rover", {.translate = {0.5, 0.0625, 1e-3}});
        const View view = scene->Select(std::vector<std::string>{"/Planet/Rover"});
        const std::vector<double> poses = WorldPoses(view);
        EXPECT_NEAR(poses[0], 6371000.625, 1e-6);
        EXPECT_NEAR(poses[1], -1e7 - 0.1875, 1e-6);
        EXPECT_NEAR(poses[2], 1e-3, 1e-6);
    });
}

TEST(WorldPosesTest, WritesPlacePrimsUnderMovedRotatedAndScaledParents) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        const Quatd quarter = FromAxisAngle({0, 0, 1}, 90);
        scene->Define<Xform>("/Car",
                             {.translate = {10, 0, 0}, .orient = quarter, .scale = {2, 2, 2}});
        scene->Define<Sphere>("/Car/Wheel");
        scene->Define<Sphere>("/Ball");
        const View view = scene->Select(std::vector<std::string>{"/Car/Wheel", "/Ball"});
        const Quatd tilt = FromAxisAngle({1, 0, 0}, 30);
        std::vector<double> targets = {12, 3, 1, tilt.x, tilt.y, tilt.z, tilt.w,
                                       -4, 5, 6, 0,      0,      0,      1};
        std::vector<int64_t> shape = {2, 7};
        view.WriteWorldPoses(HostTensor(targets, shape)).Wait();
        const std::vector<double> poses = WorldPoses(view);
        ExpectPose(poses, 0, {12, 3, 1}, tilt);
        ExpectPose(poses, 1, {-4, 5, 6}, {0, 0, 0, 1});
        const Double3 local = scene->Find("/Car/Wheel")->Get(Xformable::kTranslate);
        EXPECT_NEAR(local.x, 1.5, 1e-5);
        EXPECT_NEAR(local.y, -1, 1e-5);
        EXPECT_NEAR(local.z, 0.5, 1e-5);
        EXPECT_EQ(scene->Find("/Ball")->Get(Xformable::kTranslate).x, -4.0);
    });
}

TEST(WorldPosesTest, WritesFollowAParentMovedInTheSameStep) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Parent");
        scene->Define<Sphere>("/Parent/Child");
        const View view = scene->Select(std::vector<std::string>{"/Parent/Child"});
        scene->Find("/Parent")->Set(Xformable::kTranslate, Double3{100, 0, 0});
        std::vector<float> target = {1, 1, 1, 0, 0, 0, 1};
        std::vector<int64_t> shape = {1, 7};
        view.WriteWorldPoses(HostTensor(target, shape));
        ExpectPose(WorldPoses(view), 0, {1, 1, 1}, {0, 0, 0, 1});
        EXPECT_NEAR(scene->Find("/Parent/Child")->Get(Xformable::kTranslate).x, -99, 1e-5);
    });
}

TEST(WorldPosesTest, PrimsThatResetTheStackIgnoreTheirParents) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Parent", {.translate = {10, 0, 0}, .scale = {3, 3, 3}});
        scene->Define<Xform>("/Parent/Body", {.resetXformStack = true});
        const View view = scene->Select(std::vector<std::string>{"/Parent/Body"});
        std::vector<double> target = {1, 2, 3, 0, 0, 0, 1};
        std::vector<int64_t> shape = {1, 7};
        view.WriteWorldPoses(HostTensor(target, shape));
        ExpectPose(WorldPoses(view), 0, {1, 2, 3}, {0, 0, 0, 1});
        EXPECT_EQ(scene->Find("/Parent/Body")->Get(Xformable::kTranslate).y, 2.0);
    });
}

TEST(WorldPosesTest, WritesTakeSplitSourcesRowsMasksAndWxyz) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Base", {.translate = {0, 0, 10}});
        scene->Define<Sphere>("/Base/A");
        scene->Define<Sphere>("/Base/B", {.translate = {7, 7, 7}});
        const View view = scene->Select(std::vector<std::string>{"/Base/A", "/Base/B"});
        const Quatd half = FromAxisAngle({0, 1, 0}, 180);
        std::vector<float> translations = {9, 9, 9, 1, 2, 3};
        std::vector<float> rotations = {
            float(half.w), float(half.x), float(half.y), float(half.z), 1, 0, 0, 0};
        std::vector<int64_t> translationShape = {2, 3};
        std::vector<int64_t> rotationShape = {2, 4};
        std::vector<int32_t> sources = {1, 0};
        std::vector<int64_t> sourceShape = {2};
        DLTensor rows{};
        rows.data = sources.data();
        rows.device = {kDLCPU, 0};
        rows.ndim = 1;
        rows.dtype = {kDLInt, 32, 1};
        rows.shape = sourceShape.data();
        std::vector<uint64_t> bits = {0b01};
        std::vector<int64_t> maskShape = {1};
        DLTensor mask{};
        mask.data = bits.data();
        mask.device = {kDLCPU, 0};
        mask.ndim = 1;
        mask.dtype = {kDLUInt, 64, 1};
        mask.shape = maskShape.data();
        view.WriteWorldPoses(HostTensor(translations, translationShape),
                             HostTensor(rotations, rotationShape), Stream::None(),
                             QuaternionOrder::Wxyz, &rows, &mask);
        const std::vector<double> poses = WorldPoses(view);
        ExpectPose(poses, 0, {1, 2, 3}, {0, 0, 0, 1});
        ExpectPose(poses, 1, {7, 7, 17}, {0, 0, 0, 1});
        const std::vector<float> wxyz =
            view.ReadWorldPoses(device::DType::Float32, Stream::Host(), QuaternionOrder::Wxyz)
                .ToHost<float>();
        EXPECT_FLOAT_EQ(wxyz[3], 1.0f);
    });
}

TEST(WorldPosesTest, DeviceTensorsRoundTrip) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Xform>("/Arm",
                             {.translate = {0, 0, 2}, .orient = FromAxisAngle({0, 0, 1}, 45)});
        scene->Define<Sphere>("/Arm/Hand");
        const View hands = scene->Select(std::vector<std::string>{"/Arm/Hand"});
        const device::Tensor before = hands.ReadWorldPoses(device::DType::Float64, Stream::None());
        scene->Find("/Arm")->Set(Xformable::kTranslate, Double3{0, 0, 9});
        hands.WriteWorldPoses(before).Wait();
        ExpectPose(WorldPoses(hands), 0, {0, 0, 2}, FromAxisAngle({0, 0, 1}, 45));
    });
}

TEST(WorldPosesTest, EmptyViewsRefuse) {
    ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Sphere>("/Ball");
        const View empty;
        EXPECT_THROW(empty.ReadWorldPoses(), Error);
    });
}

}
}
