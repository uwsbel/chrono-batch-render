// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/shell/editor.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/app/viewer/shell/history.h"
#include "chrono_batch_render/app/viewer/shell/selection.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/typed.h"

namespace chrono::batch_render::app {
namespace {

class EditorTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        s_device = device::Device::Create();
    }

    static void TearDownTestSuite() {
        s_device = nullptr;
    }

    static Ref<device::Device> s_device;
};

Ref<device::Device> EditorTest::s_device;

struct Session {
    Ref<Scene> scene;
    Selection selection;
    History history;
    Editor editor{scene, selection, history};

    explicit Session(device::Device& device) : scene(Scene::Create(device)) {}

    bool Has(const std::string& path) const {
        return scene->Find(path).has_value();
    }
};

TEST(EditorClassifyTest, ClassifiesPrimsIntoGroups) {
    EXPECT_EQ(Classify(SchemaId::Xform), PrimGroup::Organize);
    EXPECT_EQ(Classify(SchemaId::Sphere), PrimGroup::Geometry);
    EXPECT_EQ(Classify(SchemaId::DistantLight), PrimGroup::Lights);
    EXPECT_EQ(Classify(SchemaId::Camera), PrimGroup::Sensors);
    EXPECT_EQ(Classify(SchemaId::Lidar), PrimGroup::Sensors);
    EXPECT_EQ(Classify(SchemaId::Radar), PrimGroup::Sensors);
    EXPECT_EQ(Classify(SchemaId::PointInstancer), PrimGroup::Geometry);
    EXPECT_EQ(Classify(SchemaId::GeomSubset), PrimGroup::Geometry);
    EXPECT_EQ(Classify(SchemaId::UsdPreviewSurface), PrimGroup::Shading);
    EXPECT_EQ(Classify(SchemaId::SensorPipeline), PrimGroup::SensorPipeline);
    EXPECT_EQ(Classify(SchemaId::Glare), PrimGroup::SensorPipeline);
    EXPECT_EQ(Classify(SchemaId::GradientBackground), PrimGroup::Backgrounds);
    EXPECT_EQ(Classify(SchemaId::RenderProduct), PrimGroup::Render);
}

TEST_F(EditorTest, CreatesUniqueChildrenOfTheSelectionWithUndo) {
    {
        Session session(*s_device);
        const std::string group = session.editor.Create(SchemaId::Xform, {}, {});
        EXPECT_EQ(group, "/Xform");
        const std::string first = session.editor.Create(SchemaId::Sphere, {1, 2, 3}, {});
        EXPECT_EQ(first, "/Xform/Sphere");
        session.selection.Set(group);
        EXPECT_EQ(session.editor.Create(SchemaId::Sphere, {}, {}), "/Xform/Sphere_1");
        EXPECT_EQ(session.selection.paths, std::vector<std::string>{"/Xform/Sphere_1"});
        EXPECT_EQ(session.scene->Find(first)->Get(Xformable::kTranslate), (Double3{1, 2, 3}));

        session.history.Undo();
        EXPECT_FALSE(session.Has("/Xform/Sphere_1"));
        EXPECT_EQ(session.selection.paths, std::vector<std::string>{group});
        session.history.Redo();
        EXPECT_TRUE(session.Has("/Xform/Sphere_1"));
    }
}

TEST_F(EditorTest, RenamesAndReparentsWithUndo) {
    {
        Session session(*s_device);
        session.scene->Define<Xform>("/A");
        session.scene->Define<Xform>("/A/Child");
        session.scene->Define<Xform>("/B");
        session.selection.Set("/A/Child");

        EXPECT_TRUE(session.editor.Rename("/A", "Renamed"));
        EXPECT_TRUE(session.Has("/Renamed/Child"));
        EXPECT_EQ(session.selection.paths, std::vector<std::string>{"/Renamed/Child"});
        EXPECT_FALSE(session.editor.Rename("/Renamed", "B"));
        session.history.Undo();
        EXPECT_TRUE(session.Has("/A/Child"));
        EXPECT_EQ(session.selection.paths, std::vector<std::string>{"/A/Child"});

        EXPECT_FALSE(session.editor.Reparent("/A", "/A/Child"));
        EXPECT_TRUE(session.editor.Reparent("/A", "/B"));
        EXPECT_TRUE(session.Has("/B/A/Child"));
        session.history.Undo();
        EXPECT_TRUE(session.Has("/A/Child"));
        EXPECT_FALSE(session.Has("/B/A"));
    }
}

TEST_F(EditorTest, DuplicatesAndDeletesTheSelectionWithUndo) {
    {
        Session session(*s_device);
        session.scene->Define<Xform>("/Group");
        session.scene->Define<Sphere>("/Group/Ball", {.radius = 2});
        session.selection.paths = {"/Group", "/Group/Ball"};

        session.editor.Duplicate();
        EXPECT_TRUE(session.Has("/Group_1/Ball"));
        EXPECT_EQ(session.scene->Find("/Group_1/Ball")->Get(Sphere::kRadius), 2);
        EXPECT_EQ(session.selection.paths, std::vector<std::string>{"/Group_1"});
        session.history.Undo();
        EXPECT_FALSE(session.Has("/Group_1"));

        session.selection.Set("/Group");
        session.editor.Delete();
        EXPECT_FALSE(session.Has("/Group"));
        session.history.Undo();
        EXPECT_TRUE(session.Has("/Group/Ball"));
        EXPECT_EQ(session.scene->Find("/Group/Ball")->Get(Sphere::kRadius), 2);

        session.selection.Clear();
        session.editor.Delete();
        session.editor.Duplicate();
        EXPECT_EQ(session.history.NextUndo(), nullptr);
        ASSERT_NE(session.history.NextRedo(), nullptr);
        EXPECT_EQ(*session.history.NextRedo(), "Delete Group");
    }
}

TEST_F(EditorTest, SetsPosesAndVisibilityWithUndo) {
    {
        Session session(*s_device);
        session.scene->Define<Sphere>("/Ball");
        const Prim ball = *session.scene->Find("/Ball");
        ball.Set(Xformable::kTranslate, Double3{4, 5, 6});
        ball.Set(Xformable::kScale, Float3{2, 2, 2});
        session.editor.SetPose("move", "/Ball", {}, {}, {1, 1, 1}, {4, 5, 6}, {}, {2, 2, 2});
        session.history.Undo();
        EXPECT_EQ(ball.Get(Xformable::kTranslate), (Double3{0, 0, 0}));
        EXPECT_EQ(ball.Get(Xformable::kScale), (Float3{1, 1, 1}));
        session.history.Redo();
        EXPECT_EQ(ball.Get(Xformable::kTranslate), (Double3{4, 5, 6}));
        EXPECT_EQ(ball.Get(Xformable::kScale), (Float3{2, 2, 2}));
        session.history.Clear();

        session.editor.SetVisible("/Ball", false);
        ASSERT_NE(session.history.NextUndo(), nullptr);
        EXPECT_EQ(*session.history.NextUndo(), "Hide Ball");
        session.history.Undo();
        EXPECT_EQ(session.history.NextUndo(), nullptr);
    }
}

TEST_F(EditorTest, SetsCellsAcrossPrimsAndRestoresEachOnUndo) {
    {
        Session session(*s_device);
        session.scene->Define<Sphere>("/A", {.radius = 1});
        session.scene->Define<Sphere>("/B", {.radius = 2});
        const std::vector<std::string> paths = {"/A", "/B"};
        const auto cells = [](std::vector<float> values) {
            std::vector<std::byte> bytes(values.size() * sizeof(float));
            std::memcpy(bytes.data(), values.data(), bytes.size());
            return bytes;
        };
        session.editor.SetCells(
            "Set Radius",
            [scene = session.scene, paths] {
                return scene->Select(Span<const std::string>(paths), SchemaId::Sphere);
            },
            Sphere::kRadius.Column(), cells({1, 2}), cells({5}));
        EXPECT_EQ(session.scene->Find("/A")->Get(Sphere::kRadius), 5);
        EXPECT_EQ(session.scene->Find("/B")->Get(Sphere::kRadius), 5);
        session.history.Undo();
        EXPECT_EQ(session.scene->Find("/A")->Get(Sphere::kRadius), 1);
        EXPECT_EQ(session.scene->Find("/B")->Get(Sphere::kRadius), 2);
        session.history.Redo();
        EXPECT_EQ(session.scene->Find("/B")->Get(Sphere::kRadius), 5);
    }
}

TEST_F(EditorTest, SetsRelationshipTargetsWithUndo) {
    {
        Session session(*s_device);
        session.scene->Define<Material>("/Red");
        session.scene->Define<Material>("/Blue");
        session.scene->Define<Sphere>("/Ball", {.materialBinding = {"/Red"}});
        const auto target = [scene = session.scene] { return scene->Select("/Ball"); };
        const ColumnRef binding = Sphere::kMaterialBinding.Column();
        session.editor.SetTargets("Set Material", target, binding, {{"/Red"}}, {"/Blue"});
        EXPECT_EQ(session.scene->Find("/Ball")->Get(Sphere::kMaterialBinding),
                  std::vector<std::string>{"/Blue"});
        session.history.Undo();
        EXPECT_EQ(session.scene->Find("/Ball")->Get(Sphere::kMaterialBinding),
                  std::vector<std::string>{"/Red"});
        session.editor.SetTargets("Clear Material", target, binding, {{"/Red"}}, {});
        EXPECT_TRUE(session.scene->Find("/Ball")->Get(Sphere::kMaterialBinding).empty());
        EXPECT_EQ(*session.history.NextUndo(), "Clear Material");
    }
}

TEST_F(EditorTest, AppliesApisToTheSelectedPrimsTheyApplyToWithUndo) {
    {
        Session session(*s_device);
        session.scene->Define<Sphere>("/Ball");
        session.scene->Define<SphereLight>("/Lamp");
        session.selection.paths = {"/Ball", "/Lamp"};

        session.editor.ApplyApi(SchemaId::ShapingAPI);
        EXPECT_FALSE(session.scene->Find("/Ball")->HasApi(SchemaId::ShapingAPI));
        EXPECT_TRUE(session.scene->Find("/Lamp")->HasApi(SchemaId::ShapingAPI));
        EXPECT_EQ(*session.history.NextUndo(), "Apply ShapingAPI");
        session.history.Undo();
        EXPECT_FALSE(session.scene->Find("/Lamp")->HasApi(SchemaId::ShapingAPI));
        session.history.Redo();
        EXPECT_TRUE(session.scene->Find("/Lamp")->HasApi(SchemaId::ShapingAPI));

        session.history.Clear();
        session.editor.ApplyApi(SchemaId::ShapingAPI);
        EXPECT_EQ(session.history.NextUndo(), nullptr);

        session.editor.ApplyApi(SchemaId::SemanticsLabelsAPI, "class");
        EXPECT_EQ(session.scene->Find("/Ball")->Instances(SchemaId::SemanticsLabelsAPI),
                  std::vector<std::string>{"class"});
        session.history.Undo();
        EXPECT_TRUE(session.scene->Find("/Ball")->Instances(SchemaId::SemanticsLabelsAPI).empty());
    }
}

std::vector<std::string> Bound(const Scene& scene, const std::string& path) {
    return scene.Select(path).Get(MaterialBindingAPI::kMaterialBinding).GetTargets(0);
}

TEST_F(EditorTest, MaterialsAreCreatedWithTheirSurfaceAndBoundToTheSelection) {
    Session session(*s_device);
    session.scene->Define<Sphere>("/Rock");
    session.selection.Set("/Rock");
    const std::string material = session.editor.CreateMaterial(SchemaId::HapkeSurface);
    EXPECT_EQ(material, "/Looks/Hapke");
    EXPECT_EQ(SurfaceShader(*session.scene, material), "/Looks/Hapke/Surface");
    EXPECT_EQ(session.scene->Find("/Looks/Hapke/Surface")->Schema(), SchemaId::HapkeSurface);
    EXPECT_EQ(Bound(*session.scene, "/Rock"), std::vector<std::string>{material});
    EXPECT_EQ(session.selection.paths, std::vector<std::string>{material});

    session.history.Undo();
    EXPECT_FALSE(session.Has("/Looks"));
    EXPECT_TRUE(Bound(*session.scene, "/Rock").empty());
    session.history.Redo();
    EXPECT_EQ(Bound(*session.scene, "/Rock"), std::vector<std::string>{material});

    session.selection.Clear();
    EXPECT_EQ(session.editor.CreateMaterial(SchemaId::UsdPreviewSurface), "/Looks/Material");
    EXPECT_EQ(Bound(*session.scene, "/Rock"), std::vector<std::string>{material});
}

TEST_F(EditorTest, ChangingTheSurfaceTypeKeepsSharedInputsAndDropsOthers) {
    Session session(*s_device);
    const std::string material = session.editor.CreateMaterial(SchemaId::HapkeSurface);
    const std::string shader = *SurfaceShader(*session.scene, material);
    const Prim surface = *session.scene->Find(shader);
    surface.Set("albedo", Float3{0.1f, 0.2f, 0.3f});
    surface.Set("normalStrength", 0.5f);
    const Prim texture = session.scene->Define(material + "/Rough", SchemaId::UsdUVTexture);
    surface.Connect("thetaBar", texture, "r");
    surface.Connect("normal", texture, "rgb");

    session.editor.SetSurfaceType(shader, SchemaId::OrenNayarSurface);
    const Prim changed = *session.scene->Find(shader);
    EXPECT_EQ(changed.Schema(), SchemaId::OrenNayarSurface);
    EXPECT_EQ(changed.Get<Float3>("albedo"), (Float3{0.1f, 0.2f, 0.3f}));
    EXPECT_EQ(changed.Get<float>("normalStrength"), 0.5f);
    const std::vector<Connection> connections = changed.Connections();
    ASSERT_EQ(connections.size(), 1u);
    EXPECT_EQ(connections.front().input, "normal");

    session.history.Undo();
    const Prim restored = *session.scene->Find(shader);
    EXPECT_EQ(restored.Schema(), SchemaId::HapkeSurface);
    EXPECT_EQ(restored.Get<Float3>("albedo"), (Float3{0.1f, 0.2f, 0.3f}));
    EXPECT_EQ(restored.Connections().size(), 2u);
}

TEST_F(EditorTest, InputsConnectToNewAndExistingNodesAndDisconnect) {
    Session session(*s_device);
    const std::string material = session.editor.CreateMaterial(SchemaId::HapkeSurface);
    const std::string shader = *SurfaceShader(*session.scene, material);

    session.editor.ConnectNode(shader, "albedo", SchemaId::UsdUVTexture, "rgb", "w.exr");
    session.editor.ConnectNode(shader, "thetaBar", SchemaId::UsdUVTexture, "r", "rough.exr");
    const Prim w = *session.scene->Find(material + "/albedoTexture");
    EXPECT_EQ(w.Get(UsdUVTexture::kFile), "w.exr");
    const Prim placement = *session.scene->Find(material + "/Placement");
    EXPECT_TRUE(placement.IsA(SchemaId::TexturePlacement));
    EXPECT_EQ(session.scene->Find(material + "/thetaBarTexture")->Connections().front().source,
              placement.Path());
    EXPECT_EQ(session.scene->Find(shader)->Connections().size(), 2u);

    session.editor.Disconnect(shader, "albedo");
    EXPECT_EQ(session.scene->Find(shader)->Connections().size(), 1u);
    session.editor.Connect(shader, "albedo", w.Path(), "rgb");
    EXPECT_EQ(session.scene->Find(shader)->Connections().size(), 2u);
    session.history.Undo();
    session.history.Undo();
    EXPECT_EQ(session.scene->Find(shader)->Connections().size(), 2u);

    session.history.Undo();
    EXPECT_FALSE(session.Has(material + "/thetaBarTexture"));
    EXPECT_TRUE(session.Has(material + "/Placement"));
    session.history.Undo();
    EXPECT_FALSE(session.Has(material + "/Placement"));
    EXPECT_TRUE(session.scene->Find(shader)->Connections().empty());
}

TEST_F(EditorTest, BindingAMaterialIsUndoable) {
    Session session(*s_device);
    session.scene->Define<Sphere>("/Rock");
    const std::string first = session.editor.CreateMaterial(SchemaId::LunarLambertSurface);
    const std::string second = session.editor.CreateMaterial(SchemaId::OrenNayarSurface);
    session.editor.Bind("/Rock", first);
    session.editor.Bind("/Rock", second);
    EXPECT_EQ(Bound(*session.scene, "/Rock"), std::vector<std::string>{second});
    session.history.Undo();
    EXPECT_EQ(Bound(*session.scene, "/Rock"), std::vector<std::string>{first});
}

TEST_F(EditorTest, MaterialsGetRadarMaterialsAndTexturesGetPlacements) {
    Session session(*s_device);
    const std::string material = session.editor.CreateMaterial(SchemaId::HapkeSurface);
    session.editor.ConnectNode(material, "radar", SchemaId::ItuMaterial, "radar", {});
    const Prim itu = *session.scene->Find(material + "/ItuMaterial");
    const std::vector<Connection> links = session.scene->Find(material)->Connections();
    EXPECT_TRUE(std::ranges::any_of(links, [&](const Connection& link) {
        return link.input == "radar" && link.source == itu.Path() && link.output == "radar";
    }));

    const Prim texture = session.scene->Define(material + "/Loose", SchemaId::UsdUVTexture);
    session.editor.ConnectNode(texture.Path(), "st", SchemaId::TexturePlacement, "st", {});
    EXPECT_TRUE(session.scene->Find(material + "/Placement")->IsA(SchemaId::TexturePlacement));
    EXPECT_EQ(session.scene->Find(texture.Path())->Connections().front().source,
              material + "/Placement");
    session.history.Undo();
    EXPECT_FALSE(session.Has(material + "/Placement"));
}

}
}
