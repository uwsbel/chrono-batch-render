// SPDX-License-Identifier: BSD-3-Clause
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <dlpack/dlpack.h>
#include <fmt/format.h>

#include "bench.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {
namespace {

constexpr uint32_t kChildren = 8;

struct Poses {
    Ref<Scene> scene;
    View parents;
    View children;
    std::vector<float> host;
    std::vector<int64_t> shape;
    device::Tensor poses;
    device::Tensor parentPoses;
    uint64_t step = 0;
};

std::vector<float> Grid(uint32_t count, float lift) {
    std::vector<float> poses;
    poses.reserve(size_t(count) * 7);
    for (uint32_t i = 0; i < count; ++i) {
        poses.insert(poses.end(), {float(i % 1000), float(i / 1000), lift, 0, 0, 0, 1});
    }
    return poses;
}

device::Tensor Upload(device::Device& device, std::vector<float>& values, int64_t rows) {
    std::vector<int64_t> shape = {rows, 7};
    DLTensor tensor{};
    tensor.data = values.data();
    tensor.device = {kDLCPU, 0};
    tensor.ndim = 2;
    tensor.dtype = {kDLFloat, 32, 1};
    tensor.shape = shape.data();
    const device::Tensor local = device.Import(tensor, Stream::None(), "bench poses");
    device::Tensor owned = device.CreateTensor(device::DType::Float32, {rows, 7}, "bench poses");
    device.Copy(local, owned).Wait();
    return owned;
}

Poses Prepare(uint32_t count) {
    Poses setup;
    setup.scene = Scene::Create(bench::BenchDevice());
    const uint32_t groups = (count + kChildren - 1) / kChildren;
    std::vector<std::string> parents;
    std::vector<std::string> children;
    for (uint32_t g = 0; g < groups; ++g) {
        parents.push_back(fmt::format("/G{}", g));
        for (uint32_t c = 0; c < kChildren && children.size() < count; ++c) {
            children.push_back(fmt::format("/G{}/B{}", g, c));
        }
    }
    setup.scene->Define<Xform>(parents);
    setup.scene->Define<Sphere>(children);
    setup.scene->Publish({.wait = true}).Wait();
    setup.parents = setup.scene->Select(parents);
    setup.children = setup.scene->Select(children);
    setup.host = Grid(count, 1);
    setup.shape = {int64_t(count), 7};
    device::Device& device = setup.scene->GetDevice();
    setup.poses = Upload(device, setup.host, count);
    std::vector<float> lifted = Grid(groups, 0);
    setup.parentPoses = Upload(device, lifted, groups);
    return setup;
}

Poses& Cached(benchmark::State& state) {
    const auto count = uint32_t(state.range(0));
    return bench::Cached<Poses>(fmt::format("world poses {}", count),
                                [&] { return Prepare(count); });
}

template <class Work> void Measure(benchmark::State& state, uint64_t prims, Work work) {
    profiler::SetHistory(32);
    profiler::Clear();
    profiler::BeginFrame();
    for (auto _ : state) {
        work();
        profiler::BeginFrame();
    }
    if (std::getenv("CHRONO_BATCH_RENDER_BENCH_ZONES") != nullptr) {
        std::printf("%s\n%s\n", state.name().c_str(),
                    profiler::ReportText(profiler::Summarize(profiler::Frames()),
                                         {.depth = 4, .minMs = 0.005})
                        .c_str());
    }
    profiler::SetHistory(2);
    state.SetItemsProcessed(int64_t(state.iterations() * prims));
    state.counters["prims"] = double(prims);
}

void BM_ReadPoses(benchmark::State& state) {
    Poses& setup = Cached(state);
    Measure(state, setup.children.PrimCount(),
            [&] { benchmark::DoNotOptimize(setup.children.ReadPoses()); });
}

void BM_ReadWorldPoses(benchmark::State& state) {
    Poses& setup = Cached(state);
    Measure(state, setup.children.PrimCount(),
            [&] { benchmark::DoNotOptimize(setup.children.ReadWorldPoses()); });
}

void BM_ReadWorldPosesToHost(benchmark::State& state) {
    Poses& setup = Cached(state);
    Measure(state, setup.children.PrimCount(),
            [&] { benchmark::DoNotOptimize(setup.children.ReadWorldPoses().ToHost<float>()); });
}

void BM_WritePoses(benchmark::State& state) {
    Poses& setup = Cached(state);
    Measure(state, setup.children.PrimCount(),
            [&] { setup.children.WritePoses(setup.poses).Wait(); });
}

void BM_WriteWorldPoses(benchmark::State& state) {
    Poses& setup = Cached(state);
    Measure(state, setup.children.PrimCount(),
            [&] { setup.children.WriteWorldPoses(setup.poses).Wait(); });
}

void BM_WriteWorldPosesFromHost(benchmark::State& state) {
    Poses& setup = Cached(state);
    DLTensor tensor{};
    tensor.data = setup.host.data();
    tensor.device = {kDLCPU, 0};
    tensor.ndim = 2;
    tensor.dtype = {kDLFloat, 32, 1};
    tensor.shape = setup.shape.data();
    Measure(state, setup.children.PrimCount(),
            [&] { setup.children.WriteWorldPoses(tensor).Wait(); });
}

void BM_MoveParentsThenReadWorldPoses(benchmark::State& state) {
    Poses& setup = Cached(state);
    Measure(state, setup.children.PrimCount(), [&] {
        setup.parents.WritePoses(setup.parentPoses);
        benchmark::DoNotOptimize(setup.children.ReadWorldPoses());
    });
}

void Sizes(benchmark::internal::Benchmark* bench) {
    for (const int64_t count : {1000, 10000, 100000, 500000}) {
        bench->Arg(count);
    }
    bench->Unit(benchmark::kMicrosecond)->UseRealTime()->MinTime(0.2);
}

BENCHMARK(BM_ReadPoses)->Apply(Sizes);
BENCHMARK(BM_ReadWorldPoses)->Apply(Sizes);
BENCHMARK(BM_ReadWorldPosesToHost)->Apply(Sizes);
BENCHMARK(BM_WritePoses)->Apply(Sizes);
BENCHMARK(BM_WriteWorldPoses)->Apply(Sizes);
BENCHMARK(BM_WriteWorldPosesFromHost)->Apply(Sizes);
BENCHMARK(BM_MoveParentsThenReadWorldPoses)->Apply(Sizes);

}
}
