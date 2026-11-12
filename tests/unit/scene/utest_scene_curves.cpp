// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/scene/tables/curves.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/bvh/builder.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/scene/schema/columns.h"
#include "chrono_batch_render/scene/tables/table.h"
#include "chrono_batch_render/stage/columns/column_store.h"
#include "chrono_batch_render/stage/publish/publisher.h"
#include "chrono_batch_render/stage/structure/structure.h"
#include "support/devices.h"

namespace chrono::batch_render {
namespace {

constexpr uint32_t kMaxRefits = 2;

const std::vector<Float3> kStraight = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
const std::vector<Float3> kBent = {{0, 2, 0}, {0, 4, 0}, {3, 4, 0}, {3, 2, 0}};

struct Scene {
    ColumnStore& store;
    Structure& structure;
    Publisher& publisher;
    CurvesTable& curves;
    Ordinal ordinal = 0;

    void Publish() {
        publisher.Publish(++ordinal).Wait();
    }

    uint32_t Define(const std::vector<Float3>& points, std::vector<int32_t> counts) {
        return structure
            .Define<BasisCurves>(
                std::vector<std::string>{"/Curves"},
                {.points = points, .curveVertexCounts = std::move(counts), .widths = {2}})
            .firstPrim;
    }

    void Move(uint32_t prim, const std::vector<Float3>& moved) {
        const uint32_t row = structure.GetPrims().Row(prim, SchemaId::PointBased);
        const ArrayRef array = store.Array(kPointBasedPoints, row);
        store.Arrays(ColumnType::Float3Array).Write(array, std::as_bytes(std::span(moved)));
        store.SetArray(kPointBasedPoints, row, array);
    }

    uint32_t Row(uint32_t prim) const {
        return structure.GetPrims().Row(prim, SchemaId::Curves);
    }
};

template <class Test>
void ForEachScene(Test test, float tolerance = 0.05f, uint32_t maxSteps = 16) {
    test::ForEachDevice([&](device::Device& device) {
        if (device.Adapter().backend == device::Backend::Hip) {
            return;
        }
        ColumnStore store(device);
        Structure structure(store);
        RenderHeader header;
        Publisher publisher(structure, &header);
        const std::unique_ptr<bvh::Builder> builder = bvh::Builder::Create(device);
        CurvesTable curves(structure, *builder, kMaxRefits, tolerance, maxSteps);
        publisher.Add(curves);
        Scene scene{store, structure, publisher, curves};
        test(scene);
    });
}

std::vector<Float3> Joined(const std::vector<Float3>& a, const std::vector<Float3>& b) {
    std::vector<Float3> joined = a;
    joined.insert(joined.end(), b.begin(), b.end());
    return joined;
}

TEST(CurvesTest, StraightSpansTakeOneStepAndBendsTakeMore) {
    ForEachScene([](Scene& s) {
        const uint32_t prim = s.Define(Joined(kStraight, kBent), {4, 4});
        s.Publish();
        EXPECT_EQ(s.curves.SpanSteps(s.Row(prim)), (std::vector<uint32_t>{1, 0, 0, 0, 8, 0, 0, 0}));
        EXPECT_EQ(s.curves.Stats().readBacks, 1u);
        EXPECT_EQ(s.curves.Record(s.Row(prim)).segmentCount, 9u + 4u);
    });
}

TEST(CurvesTest, NoToleranceTakesTheMostSteps) {
    ForEachScene(
        [](Scene& s) {
            const uint32_t prim = s.Define(kStraight, {4});
            s.Publish();
            EXPECT_EQ(s.curves.SpanSteps(s.Row(prim))[0], 5u);
        },
        0, 5);
}

TEST(CurvesTest, RefitsKeepStepsAndCurvesThatOutgrowThemGrowNextStep) {
    ForEachScene([](Scene& s) {
        const uint32_t prim = s.Define(kStraight, {4});
        s.Publish();
        const uint32_t row = s.Row(prim);
        EXPECT_EQ(s.curves.SpanSteps(row)[0], 1u);
        for (uint32_t step = 1; step <= kMaxRefits; ++step) {
            s.Move(prim, kBent);
            s.Publish();
            EXPECT_EQ(s.curves.Stats().refit, 1u) << step;
            EXPECT_EQ(s.curves.SpanSteps(row)[0], 1u) << step;
        }
        s.Move(prim, kBent);
        s.Publish();
        EXPECT_EQ(s.curves.Stats().built, 1u);
        EXPECT_EQ(s.curves.Stats().readBacks, 0u);
        EXPECT_EQ(s.curves.SpanSteps(row)[0], 1u);
        s.Move(prim, kBent);
        s.Publish();
        EXPECT_EQ(s.curves.Stats().built, 1u);
        EXPECT_EQ(s.curves.SpanSteps(row)[0], 8u);
        EXPECT_EQ(s.curves.Record(row).segmentCount, 12u);
    });
}

TEST(CurvesTest, LayoutChangesFitAtOnceAndGrowOnTheNextPublish) {
    ForEachScene([](Scene& s) {
        const uint32_t prim = s.Define(kStraight, {4});
        s.Publish();
        const uint32_t row = s.Row(prim);
        EXPECT_EQ(s.curves.SpanSteps(row)[0], 1u);
        const std::vector<int32_t> counts = {4};
        ArrayPool& ints = s.store.Arrays(ColumnType::IntArray);
        const ArrayRef array = ints.Allocate(1);
        ints.Write(array, std::as_bytes(std::span(counts)));
        s.store.SetArray(kCurvesCurveVertexCounts,
                         s.structure.GetPrims().Row(prim, SchemaId::Curves), array);
        s.Move(prim, kBent);
        s.Publish();
        EXPECT_EQ(s.curves.Stats().readBacks, 0u);
        EXPECT_EQ(s.curves.Stats().built, 1u);
        EXPECT_EQ(s.curves.SpanSteps(row)[0], 1u);
        s.store.GetDevice().GetQueue().WaitIdle();
        EXPECT_TRUE(s.curves.Pending());
        s.Publish();
        EXPECT_EQ(s.curves.Stats().readBacks, 0u);
        EXPECT_EQ(s.curves.SpanSteps(row)[0], 8u);
        EXPECT_EQ(s.curves.Record(row).segmentCount, 12u);
    });
}

}
}
