// SPDX-License-Identifier: BSD-3-Clause
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "bench.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/scene_impl.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {
namespace {

constexpr uint32_t kSide = 64;
constexpr uint32_t kWarmup = 3;
constexpr uint32_t kFrames = 2;
constexpr size_t kInflight = 2;
constexpr size_t kHistory = 32;
constexpr float kExtent = 10;
constexpr uint64_t kMaxPoints = 16u << 20;
const std::vector<std::string> kProducts = {"/Render/Product"};

enum class Layout { Hair, Grass, Cables };
enum class Motion { Stationary, Moving, Recounted };

const char* Name(Layout layout) {
    switch (layout) {
    case Layout::Hair:
        return "hair";
    case Layout::Grass:
        return "grass";
    case Layout::Cables:
        return "cables";
    }
    return "";
}

const char* Name(Motion motion) {
    switch (motion) {
    case Motion::Stationary:
        return "stationary";
    case Motion::Moving:
        return "moving";
    case Motion::Recounted:
        return "recounted";
    }
    return "";
}

struct Shape {
    Layout layout = Layout::Hair;
    Motion motion = Motion::Stationary;
    uint32_t worlds = 1;
    uint32_t curves = 1000;
    float tolerance = 0.05f;
    uint32_t maxSteps = 16;
};

uint32_t PointsPerCurve(Layout layout) {
    switch (layout) {
    case Layout::Hair:
        return 8;
    case Layout::Grass:
        return 4;
    case Layout::Cables:
        return 64;
    }
    return 4;
}

struct Strands {
    std::vector<Float3> points;
    std::vector<int32_t> counts;
    std::vector<float> widths;
    CurveBasis basis = CurveBasis::Bspline;
};

Strands Grow(Layout layout, uint32_t curves, uint32_t seed) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0, 1);
    const uint32_t per = PointsPerCurve(layout);
    Strands strands;
    strands.points.reserve(size_t(curves) * per);
    strands.counts.assign(curves, int32_t(per));
    if (layout == Layout::Hair) {
        const float width = 0.15f * kExtent / std::sqrt(float(std::max(curves, 1u)));
        for (uint32_t c = 0; c < curves; ++c) {
            const float x = (unit(random) - 0.5f) * kExtent;
            const float y = (unit(random) - 0.5f) * kExtent;
            const float lean = unit(random) * 6.2832f;
            const float curl = 0.3f + unit(random);
            const float length = kExtent * (0.2f + 0.2f * unit(random));
            for (uint32_t i = 0; i < per; ++i) {
                const float t = float(i) / float(per - 1);
                const float bend = curl * t * t;
                strands.points.push_back({x + bend * std::cos(lean + 3 * t),
                                          y + bend * std::sin(lean + 3 * t), length * t});
            }
        }
        strands.widths = {width};
    } else if (layout == Layout::Grass) {
        const float width = 0.4f * kExtent / std::sqrt(float(std::max(curves, 1u)));
        for (uint32_t c = 0; c < curves; ++c) {
            const float x = (unit(random) - 0.5f) * kExtent;
            const float y = (unit(random) - 0.5f) * kExtent;
            const float lean = unit(random) * 6.2832f;
            const float droop = kExtent * 0.05f * (0.5f + unit(random));
            const float height = kExtent * 0.08f * (0.5f + unit(random));
            for (uint32_t i = 0; i < per; ++i) {
                const float t = float(i) / float(per - 1);
                strands.points.push_back({x + droop * t * t * std::cos(lean),
                                          y + droop * t * t * std::sin(lean), height * t});
                strands.widths.push_back(width * (1 - 0.9f * t));
            }
        }
        strands.basis = CurveBasis::Bezier;
    } else {
        const float width = 0.01f * kExtent;
        for (uint32_t c = 0; c < curves; ++c) {
            const float y = (unit(random) - 0.5f) * kExtent;
            const float top = kExtent * (0.5f + 0.4f * unit(random));
            const float sag = kExtent * (0.05f + 0.2f * unit(random));
            for (uint32_t i = 0; i < per; ++i) {
                const float t = float(i) / float(per - 1);
                const float u = 2 * t - 1;
                strands.points.push_back(
                    {(t - 0.5f) * kExtent, y + 0.1f * kExtent * u * u, top - sag * (1 - u * u)});
            }
        }
        strands.widths = {width};
    }
    return strands;
}

std::vector<Float3> Swayed(const std::vector<Float3>& points, uint32_t per, uint32_t frame) {
    std::vector<Float3> swayed = points;
    const float phase = 0.3f * float(frame + 1);
    for (size_t i = 0; i < swayed.size(); ++i) {
        const float t = float(i % per) / float(per - 1);
        const float turn = phase + float((i / per) % 64) * 0.1f;
        swayed[i].x += 0.02f * kExtent * t * std::sin(turn);
        swayed[i].y += 0.02f * kExtent * t * std::cos(turn);
    }
    return swayed;
}

template <class T> void Append(std::vector<std::byte>& bytes, const std::vector<T>& items) {
    const auto* data = reinterpret_cast<const std::byte*>(items.data());
    bytes.insert(bytes.end(), data, data + items.size() * sizeof(T));
}

struct Setup {
    Ref<Scene> scene;
    Renderer renderer;
    Shape shape;
    std::optional<View> prims;
    std::optional<View> eyes;
    std::optional<UntypedAttribute> moved;
    Ref<device::Buffer> frames;
    uint64_t frameBytes = 0;
    Strands strands;
    uint64_t pixels = 0;
    double bytesPerPoint = 0;
    double segmentsPerSpan = 0;
    uint64_t step = 0;
};

std::string Path(const Shape& shape, uint32_t world) {
    return shape.worlds == 1 ? std::string("/Strands") : fmt::format("/env_{}/Strands", world);
}

Double3 Eye(uint64_t step) {
    return {0.02 * kExtent * double(step % 2), -1.6 * kExtent, 0.8 * kExtent};
}

constexpr Double3 kTarget = {0, 0, 0.3 * kExtent};

uint32_t Spans(const Shape& shape) {
    const uint32_t per = PointsPerCurve(shape.layout);
    return shape.layout == Layout::Grass ? (per - 1) / 3 : per - 3;
}

Setup Prepare(const Shape& shape, device::Traversal traversal) {
    device::Device& device = bench::BenchDevice(traversal);
    const uint64_t before = device.MemoryUsage().deviceBytes;
    const Ref<Scene> scene =
        Scene::Create(device, {.curveTolerance = shape.tolerance, .maxCurveSteps = shape.maxSteps});
    Setup setup{scene, Renderer(scene), shape};
    setup.strands = Grow(shape.layout, shape.curves, 7);
    if (shape.worlds > 1) {
        scene->Define<World>("/env_0");
    }
    scene->Define<BasisCurves>(Path(shape, 0),
                               {.displayColor = {0.5f, 0.4f, 0.3f},
                                .points = setup.strands.points,
                                .curveVertexCounts = setup.strands.counts,
                                .widths = setup.strands.widths,
                                .widthsInterpolation = setup.strands.widths.size() == 1
                                                           ? Interpolation::Constant
                                                           : Interpolation::Vertex,
                                .basis = setup.strands.basis});
    const std::string camera = shape.worlds == 1 ? "/Eye" : "/env_0/Eye";
    scene->Define<Camera>(camera, {.translate = Eye(0), .orient = LookAt(Eye(0), kTarget)});
    std::vector<std::string> prims = {Path(shape, 0)};
    std::vector<std::string> views = {camera};
    if (shape.worlds > 1) {
        std::vector<std::string> targets;
        for (uint32_t w = 1; w < shape.worlds; ++w) {
            targets.push_back(fmt::format("/env_{}", w));
            prims.push_back(Path(shape, w));
            views.push_back(fmt::format("/env_{}/Eye", w));
        }
        scene->Clone(*scene->Find("/env_0"), targets);
    }
    setup.prims = scene->Select(prims);
    setup.eyes = scene->Select(views);
    scene->Define<DistantLight>("/Sun", {.orient = FromAzimuthElevation(60, 50)});
    scene->Define<Scope>("/Render");
    scene->Define<RenderVar>("/Render/Color", {.sourceName = "LdrColor"});
    scene->Define<RenderProduct>("/Render/Product", {.mode = MinimalMode::DisplayColor,
                                                     .castShadows = true,
                                                     .camera = views,
                                                     .resolution = {kSide, kSide},
                                                     .orderedVars = {"/Render/Color"}});
    setup.pixels = uint64_t(shape.worlds) * kSide * kSide;
    if (shape.motion == Motion::Moving) {
        std::vector<std::byte> frames;
        const uint32_t per = PointsPerCurve(shape.layout);
        for (uint32_t f = 0; f < kFrames; ++f) {
            for (uint32_t w = 0; w < shape.worlds; ++w) {
                Append(frames, Swayed(setup.strands.points, per, f));
            }
        }
        setup.frames =
            device.CreateBuffer({.label = "curves frames"}, std::span<const std::byte>(frames));
        setup.frameBytes = frames.size() / kFrames;
        setup.moved = setup.prims->GetArray<Float3>("points");
        Mapping mapping = setup.moved->Map(MapAccess::Write);
        const Span<const int64_t> extents = mapping.Values().Shape();
        const Span<const int64_t> strides = mapping.Values().Strides();
        int64_t expected = 1;
        for (size_t axis = extents.size(); axis-- > 0;) {
            if (strides[axis] != expected) {
                throw Error("the mapped values aren't contiguous");
            }
            expected *= extents[axis];
        }
        mapping.Close();
    }
    setup.scene->Advance(1.0 / 60);
    setup.renderer.Step(kProducts).Wait();
    const CurvesStats& stats = SceneImpl::Get(*scene).GetCurves().Stats();
    setup.segmentsPerSpan =
        double(stats.segments) / (double(shape.worlds) * shape.curves * Spans(shape));
    for (uint32_t i = 1; i < kWarmup; ++i) {
        setup.scene->Advance(1.0 / 60);
        setup.renderer.Step(kProducts).Wait();
    }
    const uint64_t frameBytes = setup.frames ? setup.frames->Bytes() : 0;
    setup.bytesPerPoint = double(device.MemoryUsage().deviceBytes - before - frameBytes) /
                          (double(shape.worlds) * setup.strands.points.size());
    return setup;
}

void Move(Setup& setup) {
    ++setup.step;
    setup.eyes->Get<Double3>("translate").Set(Eye(setup.step));
    switch (setup.shape.motion) {
    case Motion::Stationary:
        break;
    case Motion::Moving: {
        Mapping mapping = setup.moved->Map(MapAccess::Write);
        const device::Tensor& values = mapping.Values();
        device::Queue& queue = setup.scene->GetDevice().GetQueue();
        device::CommandEncoder encoder = queue.CreateEncoder();
        encoder.Copy(setup.frames->Slice(setup.step % kFrames * setup.frameBytes, setup.frameBytes),
                     values.GetBuffer().Slice(values.Offset(), setup.frameBytes));
        queue.Submit(encoder.Finish());
        mapping.Close();
        break;
    }
    case Motion::Recounted: {
        const uint32_t per = PointsPerCurve(setup.shape.layout);
        const size_t curves = setup.step % 2 == 0 ? setup.strands.counts.size()
                                                  : setup.strands.counts.size() * 9 / 10;
        const std::vector<Float3> points(setup.strands.points.begin(),
                                         setup.strands.points.begin() + ptrdiff_t(curves * per));
        const std::vector<int32_t> counts(curves, int32_t(per));
        ArrayAttribute<Float3> pointsAttribute = setup.prims->GetArray<Float3>("points");
        ArrayAttribute<int32_t> countsAttribute =
            setup.prims->GetArray<int32_t>("curveVertexCounts");
        for (uint32_t w = 0; w < setup.shape.worlds; ++w) {
            pointsAttribute.Set(w, points);
            countsAttribute.Set(w, counts);
            if (setup.strands.widths.size() > 1) {
                setup.prims->GetArray<float>("widths").Set(
                    w, std::vector<float>(setup.strands.widths.begin(),
                                          setup.strands.widths.begin() + ptrdiff_t(curves * per)));
            }
        }
        break;
    }
    }
}

void Run(benchmark::State& state, const Shape& shape, bool hardware) {
    const device::Traversal traversal =
        hardware ? device::Traversal::Hardware : device::Traversal::Software;
    Setup* setup = nullptr;
    try {
        setup = &bench::Cached<Setup>(fmt::format("curves/{}/{}/{}/{}/{}/{}/{}", Name(shape.layout),
                                                  Name(shape.motion), shape.worlds, shape.curves,
                                                  shape.tolerance, shape.maxSteps, hardware),
                                      [&] { return Prepare(shape, traversal); });
    } catch (const UnsupportedError& error) {
        state.SkipWithMessage(error.what());
        return;
    }
    profiler::SetHistory(kHistory);
    profiler::Clear();
    profiler::BeginFrame();
    std::deque<RenderOutput> flight;
    for (auto _ : state) {
        Move(*setup);
        setup->scene->Advance(1.0 / 60);
        flight.push_back(setup->renderer.Step(kProducts));
        if (flight.size() >= kInflight) {
            flight.front().Wait();
            flight.pop_front();
        }
        profiler::BeginFrame();
    }
    for (const RenderOutput& output : flight) {
        output.Wait();
    }
    profiler::BeginFrame();
    double step = 0;
    double steps = 0;
    double segments = 0;
    double blas = 0;
    double tlas = 0;
    double render = 0;
    double other = 0;
    for (const profiler::ReportTrack& track : profiler::Summarize(profiler::Frames()).tracks) {
        for (const profiler::ReportNode& zone : track.zones) {
            if (!track.gpu) {
                step += zone.name == "Render/Step" ? zone.ms : 0;
            } else if (zone.name == "Curves/Steps") {
                steps += zone.ms;
            } else if (zone.name == "Curves/Segments") {
                segments += zone.ms;
            } else if (zone.name == "Curves/Blas") {
                blas += zone.ms;
            } else if (zone.name.starts_with("Bvh/") || zone.name.starts_with("Instances/")) {
                tlas += zone.ms;
            } else if (zone.name.starts_with("Render/")) {
                render += zone.ms;
            } else {
                other += zone.ms;
            }
        }
    }
    profiler::SetHistory(2);
    const double iterations = double(state.iterations());
    const double curves = double(shape.worlds) * shape.curves;
    state.counters["curves"] = curves;
    state.counters["curves/s"] =
        benchmark::Counter(iterations * curves, benchmark::Counter::kIsRate);
    state.counters["px/s"] =
        benchmark::Counter(iterations * double(setup->pixels), benchmark::Counter::kIsRate);
    state.counters["B/point"] = setup->bytesPerPoint;
    state.counters["segments/span"] = setup->segmentsPerSpan;
    state.counters["cpu ms/step"] = step;
    state.counters["gpu steps ms"] = steps;
    state.counters["gpu segments ms"] = segments;
    state.counters["gpu blas ms"] = blas;
    state.counters["gpu tlas ms"] = tlas;
    state.counters["gpu render ms"] = render;
    state.counters["gpu other ms"] = other;
}

void Single(benchmark::State& state, Layout layout, Motion motion) {
    Run(state, {.layout = layout, .motion = motion, .curves = uint32_t(state.range(0))},
        state.range(1) != 0);
}

void Batched(benchmark::State& state, Layout layout, Motion motion) {
    Run(state,
        {.layout = layout,
         .motion = motion,
         .worlds = uint32_t(state.range(0)),
         .curves = uint32_t(state.range(1))},
        state.range(2) != 0);
}

void Tolerance(benchmark::State& state, Layout layout) {
    Run(state,
        {.layout = layout,
         .motion = Motion::Moving,
         .curves = uint32_t(state.range(0)),
         .tolerance = float(state.range(1)) / 1000,
         .maxSteps = 16},
        state.range(2) != 0);
}

const bool kCore =
    bench::Core("^Render/Curves/World/hair/motion:(stationary|moving)/curves:100000/") &&
    bench::Core("^Render/Curves/Worlds/hair/motion:moving/worlds:1024/curves:1000/");

const bool kRegistered = [] {
    const std::vector<int64_t> hardware = {0, 1};
    const std::pair<Layout, std::vector<int64_t>> singles[] = {
        {Layout::Hair, {1000, 10000, 100000, 1000000}},
        {Layout::Grass, {1000, 10000, 100000, 1000000}},
        {Layout::Cables, {16, 256, 4096}},
    };
    for (const auto& [layout, counts] : singles) {
        for (const Motion motion : {Motion::Stationary, Motion::Moving, Motion::Recounted}) {
            benchmark::RegisterBenchmark(
                fmt::format("Render/Curves/World/{}/motion:{}", Name(layout), Name(motion)), Single,
                layout, motion)
                ->ArgsProduct({counts, hardware})
                ->ArgNames({"curves", "hardware"})
                ->Unit(benchmark::kMillisecond)
                ->UseRealTime();
        }
    }
    for (const Motion motion : {Motion::Stationary, Motion::Moving, Motion::Recounted}) {
        benchmark::internal::Benchmark* registered = benchmark::RegisterBenchmark(
            fmt::format("Render/Curves/Worlds/hair/motion:{}", Name(motion)), Batched, Layout::Hair,
            motion);
        registered->ArgNames({"worlds", "curves", "hardware"});
        for (const int64_t worlds : {16, 256, 1024, 4096}) {
            for (const int64_t curves : {100, 1000}) {
                if (uint64_t(worlds * curves) * PointsPerCurve(Layout::Hair) > kMaxPoints) {
                    continue;
                }
                for (const int64_t traversal : hardware) {
                    registered->Args({worlds, curves, traversal});
                }
            }
        }
        registered->Unit(benchmark::kMillisecond)->UseRealTime();
    }
    for (const Layout layout : {Layout::Hair, Layout::Cables}) {
        benchmark::RegisterBenchmark(fmt::format("Render/Curves/Tolerance/{}", Name(layout)),
                                     Tolerance, layout)
            ->ArgsProduct({{layout == Layout::Hair ? 100000 : 256}, {0, 10, 50, 200}, hardware})
            ->ArgNames({"curves", "tolerance_per_mille", "hardware"})
            ->Unit(benchmark::kMillisecond)
            ->UseRealTime();
    }
    return true;
}();

}
}
