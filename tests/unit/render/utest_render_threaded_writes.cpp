// SPDX-License-Identifier: BSD-3-Clause
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <dlpack/dlpack.h>
#include <gtest/gtest.h>

#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/render/ray_cast/ray_cast.h"
#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/scene_impl.h"
#include "chrono_batch_render/util/memory.h"
#include "support/devices.h"
#include "support/threads.h"
#include "support/world.h"

namespace chrono::batch_render {
namespace {

using device::Device;
using device::DType;
using device::Tensor;
using test::ForEachDevice;

constexpr uint32_t kWidth = 9;
constexpr uint32_t kHeight = 7;
constexpr int kSteps = 40;
constexpr int kMoves = 400;
constexpr int kDefined = 100;
const std::vector<std::string> kProducts = {"/Render/View"};

Ref<Scene> BallScene(Device& device) {
    const Ref<Scene> scene = Scene::Create(device);
    scene->Define<Camera>("/Cam",
                          {.translate = {0, -8, 0}, .orient = LookAt({0, -8, 0}, {0, 0, 0})});
    scene->Define<Sphere>("/Ball", {.translate = {5, 0, 0}, .radius = 1});
    scene->Define<Scope>("/Render");
    scene->Define<RenderVar>("/Render/Depth", {.sourceName = "DistanceToCamera"});
    scene->Define<RenderProduct>(
        "/Render/View",
        {.camera = {"/Cam"}, .resolution = {kWidth, kHeight}, .orderedVars = {"/Render/Depth"}});
    scene->Select("/Render/View").Get<RenderMode>("renderMode").Set(RenderMode::Minimal);
    return scene;
}

float CenterDepth(RenderOutput& output) {
    output.Wait();
    const std::vector<float> depth =
        output["/Render/View"].frames.at(0).Source("DistanceToCamera").tensor.ToHost<float>();
    return depth[(kHeight / 2) * kWidth + kWidth / 2];
}

Tensor Floats(Device& device, const std::vector<float>& values, int64_t columns) {
    Tensor tensor =
        device.CreateTensor(DType::Float32, {int64_t(values.size()) / columns, columns}, "rays");
    device.GetQueue().Write(
        tensor.GetBuffer().Slice(tensor.Offset(), values.size() * sizeof(float)), values.data());
    return tensor;
}

DLTensor HostTensor(std::vector<float>& values, std::vector<int64_t>& shape) {
    DLTensor tensor{};
    tensor.data = values.data();
    tensor.device = {kDLCPU, 0};
    tensor.ndim = int32_t(shape.size());
    tensor.dtype = {kDLFloat, 32, 1};
    tensor.shape = shape.data();
    return tensor;
}

class Worker {
  public:
    template <class Work> explicit Worker(Work work) {
        m_thread = std::thread([this, work] {
            try {
                work();
            } catch (...) {
                m_error = std::current_exception();
            }
        });
    }

    void Join() {
        m_thread.join();
        if (m_error) {
            std::rethrow_exception(m_error);
        }
    }

  private:
    std::thread m_thread;
    std::exception_ptr m_error;
};

TEST(ThreadedWritesTest, WritersCastersAndARendererShareAScene) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = BallScene(device);
        Renderer renderer(scene);
        RayCaster caster(scene);
        Rays rays;
        rays.origins = Floats(device, {0, -8, 0}, 3);
        rays.directions = Floats(device, {0, 1, 0}, 3);
        const View ball = scene->Select("/Ball");
        std::atomic<bool> rendering = true;

        Worker mover([&] {
            std::vector<float> translation(3);
            std::vector<float> rotation = {0, 0, 0, 1};
            std::vector<int64_t> translationShape = {1, 3};
            std::vector<int64_t> rotationShape = {1, 4};
            for (int move = 0; move < kMoves; ++move) {
                translation = {float(5 - 5 * (move + 1) / kMoves), 0, 0};
                ball.WritePoses(HostTensor(translation, translationShape),
                                HostTensor(rotation, rotationShape));
            }
        });
        Worker definer([&] {
            for (int i = 0; i < kDefined; ++i) {
                scene->Define<Cube>("/Far" + std::to_string(i), {.translate = {double(i), 0, 100}});
            }
        });
        Worker casting([&] {
            while (rendering) {
                const RayHits hits = caster.Cast(rays);
                const std::vector<uint32_t> hit = hits.hit.ToHost<uint32_t>();
                ASSERT_EQ(hit.size(), 1u);
            }
        });

        for (int step = 0; step < kSteps; ++step) {
            scene->Advance(1.0 / 60);
            RenderOutput output = renderer.Step(kProducts);
            output.Wait();
        }
        mover.Join();
        definer.Join();
        rendering = false;
        casting.Join();

        RenderOutput output = renderer.Step(kProducts);
        EXPECT_NEAR(CenterDepth(output), 7, 1e-3);
        EXPECT_EQ(scene->PrimCount(), uint32_t(5 + kDefined));
        const std::vector<float> distance = caster.Cast(rays).distance.ToHost<float>();
        EXPECT_NEAR(distance[0], 7, 1e-3);
        EXPECT_NEAR(scene->Time(), kSteps / 60.0, 1e-9);
    });
}

TEST(ThreadedWritesTest, ManyThreadsMixEveryOperation) {
    ForEachDevice([](Device& device) {
        const Ref<Scene> scene = BallScene(device);
        const uint32_t base = scene->PrimCount();
        Renderer renderer(scene);
        RayCaster caster(scene);
        Rays rays;
        rays.origins = Floats(device, {0, -8, 0}, 3);
        rays.directions = Floats(device, {0, 1, 0}, 3);
        constexpr size_t kAuthors = 6;
        static constexpr int kRounds = 150;
        std::atomic<size_t> authoring = kAuthors;
        std::atomic<int> advances = 0;
        std::vector<std::map<std::string, Double3>> placed(kAuthors);

        const auto author = [&](size_t index) {
            std::mt19937 random(uint32_t(index) + 11);
            std::map<std::string, Double3>& mine = placed[index];
            int next = 0;
            for (int round = 0; round < kRounds; ++round) {
                const uint32_t action = mine.empty() ? 0 : random() % 8;
                if (action == 0) {
                    const std::string path =
                        "/T" + std::to_string(index) + "_" + std::to_string(next++);
                    const Double3 at{double(index), double(next), 100};
                    scene->Define<Cube>(path, {.translate = at});
                    mine[path] = at;
                    continue;
                }
                auto chosen = mine.begin();
                std::advance(chosen, random() % mine.size());
                const Prim prim = *scene->Find(chosen->first);
                if (action == 1) {
                    scene->Remove(prim);
                    mine.erase(chosen);
                } else if (action == 2) {
                    const Double3 at{double(round), double(index), 100};
                    prim.Set(Xformable::kTranslate, at);
                    chosen->second = at;
                } else if (action == 3) {
                    std::vector<std::string> paths;
                    for (const auto& [path, at] : mine) {
                        paths.push_back(path);
                    }
                    std::vector<float> translations;
                    std::vector<float> rotations;
                    for (size_t i = 0; i < paths.size(); ++i) {
                        const Double3 at{double(i), double(round), 100};
                        translations.insert(translations.end(),
                                            {float(at.x), float(at.y), float(at.z)});
                        rotations.insert(rotations.end(), {0, 0, 0, 1});
                        mine[paths[i]] = at;
                    }
                    std::vector<int64_t> translationShape = {int64_t(paths.size()), 3};
                    std::vector<int64_t> rotationShape = {int64_t(paths.size()), 4};
                    scene->Select(Span<const std::string>(paths))
                        .WritePoses(HostTensor(translations, translationShape),
                                    HostTensor(rotations, rotationShape));
                } else if (action == 4) {
                    const Double3 at = prim.Get(Xformable::kTranslate);
                    ASSERT_NEAR(at.x, chosen->second.x, 1e-4) << chosen->first;
                    ASSERT_NEAR(at.y, chosen->second.y, 1e-4) << chosen->first;
                } else if (action == 5) {
                    ASSERT_TRUE(scene->Find("/Ball"));
                    ASSERT_GE(scene->PrimCount(), base);
                    ASSERT_EQ(prim.Path(), chosen->first);
                    ASSERT_FALSE(
                        scene->Select("/T" + std::to_string(index) + "_*").Prims().empty());
                    ASSERT_GE(scene->Find("/Render")->Children().size(), 2u);
                    const device::Tensor poses = scene->Select(prim).ReadPoses();
                    ASSERT_EQ(poses.ToHost<float>().size(), 7u);
                    ASSERT_EQ(scene->Select(prim).Get(Xformable::kTranslate).ReadHost().size(),
                              sizeof(Double3));
                } else if (action == 6) {
                    ASSERT_TRUE(scene->Bounds());
                    ASSERT_EQ(scene->PrimBounds(std::vector<std::string>{chosen->first}).size(),
                              1u);
                    ASSERT_EQ(test::WorldPlaces(*scene, {chosen->first}).size(), 1u);
                    device::Download all = scene->ReadAllBoundsAsync();
                    all.Wait();
                    device::Download one =
                        scene->ReadPrimBoundsAsync(std::vector<std::string>{chosen->first});
                    one.Wait();
                } else {
                    scene->Advance(0.001);
                    ++advances;
                }
            }
            --authoring;
        };

        std::vector<std::function<void()>> tasks;
        for (size_t index = 0; index < kAuthors; ++index) {
            tasks.emplace_back([&, index] { author(index); });
        }
        tasks.emplace_back([&] {
            while (authoring > 0) {
                RenderOutput output = renderer.Step(kProducts);
                output.Wait();
            }
        });
        tasks.emplace_back([&] {
            while (authoring > 0) {
                ASSERT_EQ(caster.Cast(rays).hit.ToHost<uint32_t>().size(), 1u);
            }
        });
        tasks.emplace_back([&] {
            while (authoring > 0) {
                scene->Publish().Wait();
                ASSERT_EQ(test::WorldPlaces(*scene, {"/Ball"}).size(), 1u);
            }
        });
        test::WithinDeadline(std::chrono::seconds(300),
                             "ThreadedWritesTest.ManyThreadsMixEveryOperation",
                             [&] { test::RunTogether(tasks); });

        size_t live = 0;
        for (const std::map<std::string, Double3>& mine : placed) {
            live += mine.size();
            for (const auto& [path, at] : mine) {
                const std::optional<Prim> prim = scene->Find(path);
                ASSERT_TRUE(prim) << path;
                const Double3 found = prim->Get(Xformable::kTranslate);
                EXPECT_NEAR(found.x, at.x, 1e-4) << path;
                EXPECT_NEAR(found.y, at.y, 1e-4) << path;
                EXPECT_NEAR(found.z, at.z, 1e-4) << path;
            }
        }
        EXPECT_EQ(scene->PrimCount(), base + uint32_t(live));
        EXPECT_NEAR(scene->Time(), advances * 0.001, 1e-9);
        RenderOutput output = renderer.Step(kProducts);
        output.Wait();
    });
}

TEST(ThreadedWritesTest, LongRunsKeepMemoryFlat) {
    ForEachDevice([](Device& device) {
        const char* soak = std::getenv("CHRONO_BATCH_RENDER_TEST_SOAK");
        const auto duration = soak != nullptr && std::string_view(soak) == "1"
                                  ? std::chrono::seconds(600)
                                  : std::chrono::seconds(10);
        const auto start = std::chrono::steady_clock::now();
        const auto warmupEnd = start + duration / 5;
        const auto end = start + duration;
        const Ref<Scene> scene = BallScene(device);
        Renderer renderer(scene);
        const View ball = scene->Select("/Ball");
        std::atomic<bool> running = true;
        uint64_t warmHost = 0;
        uint64_t warmDevice = 0;
        static constexpr uint64_t kBudget = 128ull * 1024 * 1024;
        const uint64_t startHost = ResidentBytes().value();
        int overBudgetAt = -1;
        uint64_t overBudget = 0;
        test::WithinDeadline(
            duration + std::chrono::seconds(60), "ThreadedWritesTest.LongRunsKeepMemoryFlat", [&] {
                test::RunTogether(
                    {[&] {
                         std::vector<float> translation = {0, 0, 0};
                         std::vector<float> rotation = {0, 0, 0, 1};
                         std::vector<int64_t> translationShape = {1, 3};
                         std::vector<int64_t> rotationShape = {1, 4};
                         for (int move = 0; running; ++move) {
                             translation[0] = float(move % 100) * 0.01f;
                             ball.WritePoses(HostTensor(translation, translationShape),
                                             HostTensor(rotation, rotationShape));
                         }
                     },
                     [&] {
                         const Prim prim = *scene->Find("/Ball");
                         for (int edit = 0; running; ++edit) {
                             prim.Set(Sphere::kRadius, 1 + float(edit % 7) * 0.01f);
                             std::this_thread::yield();
                         }
                     },
                     [&] {
                         bool warm = false;
                         for (int step = 0; std::chrono::steady_clock::now() < end; ++step) {
                             const uint64_t resident = ResidentBytes().value();
                             if (resident > startHost + kBudget) {
                                 overBudgetAt = step;
                                 overBudget = resident;
                                 break;
                             }
                             scene->Advance(1.0 / 60);
                             RenderOutput output = renderer.Step(kProducts);
                             output.Wait();
                             if (!warm && std::chrono::steady_clock::now() >= warmupEnd) {
                                 warm = true;
                                 warmHost = ResidentBytes().value();
                                 warmDevice = device.MemoryUsage().deviceBytes;
                             }
                         }
                         running = false;
                     }});
            });
        ASSERT_EQ(overBudgetAt, -1) << "host memory grew from " << startHost << " to " << overBudget
                                    << " bytes by step " << overBudgetAt;
        const Publisher& publisher = SceneImpl::Get(*scene).GetPublisher();
        EXPECT_LE(publisher.SealedHistory(), 2 * Publisher::kSealedHistory);
        const uint64_t host = ResidentBytes().value();
        const uint64_t onDevice = device.MemoryUsage().deviceBytes;
        EXPECT_LT(host, warmHost + 32ull * 1024 * 1024)
            << "host memory grew from " << warmHost << " to " << host << " bytes";
        EXPECT_LE(onDevice, warmDevice + 16ull * 1024 * 1024)
            << "device memory grew from " << warmDevice << " to " << onDevice << " bytes";
    });
}

}
}
