// SPDX-License-Identifier: BSD-3-Clause
#include <atomic>
#include <chrono>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/scene_impl.h"
#include "chrono_batch_render/stage/store_lock.h"
#include "chrono_batch_render/util/error.h"
#include "support/devices.h"
#include "support/threads.h"
#include "support/world.h"

namespace chrono::batch_render {
namespace {

bool TakenElsewhere(const StoreLock& lock) {
    bool taken = false;
    std::thread([&] { taken = lock.TryLock().has_value(); }).join();
    return taken;
}

TEST(StoreLockTest, IsRecursiveAndKeepsOtherThreadsOut) {
    StoreLock lock;
    EXPECT_FALSE(lock.HeldByThisThread());
    EXPECT_THROW(lock.Check("Testing"), Error);
    {
        const StoreLock::Guard outer = lock.Lock();
        const StoreLock::Guard inner = lock.Lock();
        EXPECT_TRUE(lock.HeldByThisThread());
        EXPECT_NO_THROW(lock.Check("Testing"));
        EXPECT_FALSE(TakenElsewhere(lock));
    }
    EXPECT_FALSE(lock.HeldByThisThread());
    EXPECT_TRUE(TakenElsewhere(lock));
}

TEST(StoreLockTest, WritingAScenesStoreWithoutTheLockThrows) {
    test::ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        Structure& structure = SceneImpl::Get(*scene).GetStructure();
        const std::string paths[] = {"/Loose"};
        EXPECT_THROW(structure.Define(Xform::kSchema, paths), Error);
        EXPECT_FALSE(scene->Find("/Loose"));
        {
            const StoreLock::Guard lock = SceneImpl::Get(*scene).Lock();
            EXPECT_NO_THROW(structure.Define(Xform::kSchema, paths));
        }
        EXPECT_TRUE(scene->Find("/Loose"));
    });
}

TEST(StoreLockTest, ThreadsEditWhileAnotherPublishes) {
    test::ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        const Prim mover = scene->Define<Xform>("/Mover");
        static constexpr int kDefined = 64;
        static constexpr int kMoves = 200;
        std::thread definer([&] {
            for (int i = 0; i < kDefined; ++i) {
                scene->Define<Xform>("/Spawned_" + std::to_string(i));
            }
        });
        std::thread moving([&] {
            for (int i = 0; i < kMoves; ++i) {
                mover.Set(Xformable::kTranslate, Double3{double(i), 0, 0});
            }
        });
        for (int i = 0; i < 50; ++i) {
            scene->Advance(0.01);
            scene->Publish().Wait();
        }
        definer.join();
        moving.join();
        EXPECT_EQ(scene->PrimCount(), uint32_t(kDefined + 1));
        EXPECT_NEAR(test::LocateWorldPlace(*scene, "/Mover").position.x, kMoves - 1, 1e-9);
        EXPECT_NEAR(scene->Time(), 0.5, 1e-9);
    });
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

TEST(StoreLockTest, PoseWritesDontWaitForAnotherThreadsLock) {
    test::ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        const Prim mover = scene->Define<Xform>("/Mover");
        const View selection = scene->Select(mover);
        std::vector<float> translation = {4, 5, 6};
        std::vector<float> rotation = {0, 0, 0, 1};
        std::vector<int64_t> translationShape = {1, 3};
        std::vector<int64_t> rotationShape = {1, 4};
        {
            const StoreLock::Guard held = SceneImpl::Get(*scene).Lock();
            std::thread writer([&] {
                selection.WritePoses(HostTensor(translation, translationShape),
                                     HostTensor(rotation, rotationShape));
                translation = {-1, -1, -1};
            });
            writer.join();
            const Double3 before = mover.Get(Xformable::kTranslate);
            EXPECT_EQ(before.x, 0);
        }
        scene->Publish().Wait();
        const Double3 after = mover.Get(Xformable::kTranslate);
        EXPECT_NEAR(after.x, 4, 1e-6);
        EXPECT_NEAR(after.y, 5, 1e-6);
        EXPECT_NEAR(after.z, 6, 1e-6);
    });
}

TEST(StoreLockTest, TensorWritesDontWaitForAnotherThreadsLock) {
    test::ForEachDevice([](device::Device& device) {
        const Ref<Scene> scene = Scene::Create(device);
        scene->Define<Sphere>("/A");
        scene->Define<Sphere>("/B");
        UntypedAttribute radius = scene->Select("/*", Sphere::kSchema).Get("radius");
        std::vector<float> radii = {2, 3};
        std::vector<int64_t> shape = {2};
        {
            const StoreLock::Guard held = SceneImpl::Get(*scene).Lock();
            std::thread writer([&] { radius.Write(HostTensor(radii, shape)); });
            writer.join();
            radii = {-1, -1};
        }
        const std::optional<Prim> a = scene->Find("/A");
        const std::optional<Prim> b = scene->Find("/B");
        ASSERT_TRUE(a && b);
        EXPECT_EQ(a->Get(Sphere::kRadius), 2);
        EXPECT_EQ(b->Get(Sphere::kRadius), 3);
    });
}

constexpr auto kDeadline = std::chrono::seconds(180);

TEST(StoreLockStressTest, DeferredWritesAllRunExactlyOnce) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        StoreLock lock;
        std::atomic<int> deferred = 0;
        std::atomic<int> ran = 0;
        test::WithinDeadline(kDeadline, "StoreLockStressTest.DeferredWritesAllRunExactlyOnce", [&] {
            test::RunTogether(8, [&](size_t thread) {
                std::mt19937 random(uint32_t(thread * 97 + attempt));
                for (int round = 0; round < 2000; ++round) {
                    const uint32_t action = random() % 4;
                    if (action == 0) {
                        ++deferred;
                        lock.Defer([&] { ++ran; });
                    } else if (action == 1) {
                        const StoreLock::Guard outer = lock.Lock();
                        if (random() % 2 == 0) {
                            const StoreLock::Guard inner = lock.Lock();
                            ++deferred;
                            lock.Defer([&] { ++ran; });
                        }
                    } else if (action == 2) {
                        if (const auto taken = lock.TryLock()) {
                            std::this_thread::yield();
                        }
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        });
        EXPECT_EQ(ran.load(), deferred.load()) << "attempt " << attempt;
    }
}

TEST(StoreLockStressTest, DeferredWritesMayDeferMore) {
    StoreLock lock;
    std::atomic<int> ran = 0;
    std::function<void(int)> chain = [&](int left) {
        ++ran;
        if (left > 0) {
            lock.Defer([&chain, left] { chain(left - 1); });
        }
    };
    test::WithinDeadline(kDeadline, "StoreLockStressTest.DeferredWritesMayDeferMore", [&] {
        {
            const StoreLock::Guard held = lock.Lock();
            std::thread([&] { lock.Defer([&] { chain(50); }); }).join();
            EXPECT_EQ(ran, 0);
        }
    });
    EXPECT_EQ(ran, 51);
}

TEST(StoreLockStressTest, AThrowingDeferredWriteDoesntStopTheRest) {
    StoreLock lock;
    std::atomic<int> ran = 0;
    {
        const StoreLock::Guard held = lock.Lock();
        std::thread([&] {
            lock.Defer([&] { ++ran; });
            lock.Defer([] { throw std::runtime_error("deferred write failed on purpose"); });
            lock.Defer([&] { ++ran; });
        }).join();
    }
    EXPECT_EQ(ran, 2);
}

std::atomic<int> g_blockingWaits = 0;

void CountingWait(void (*wait)(void*), void* context) {
    ++g_blockingWaits;
    wait(context);
}

TEST(StoreLockStressTest, TheBlockingWaitHookRunsOnlyWhenContended) {
    StoreLock lock;
    StoreLock::SetBlockingWait(CountingWait);
    g_blockingWaits = 0;
    {
        const StoreLock::Guard free = lock.Lock();
        const StoreLock::Guard again = lock.Lock();
    }
    EXPECT_EQ(g_blockingWaits, 0);
    {
        std::atomic<bool> holding = false;
        std::atomic<bool> release = false;
        std::thread holder([&] {
            const StoreLock::Guard held = lock.Lock();
            holding = true;
            while (!release) {
                std::this_thread::yield();
            }
        });
        while (!holding) {
            std::this_thread::yield();
        }
        std::thread waiter([&] { const StoreLock::Guard waited = lock.Lock(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        release = true;
        holder.join();
        waiter.join();
    }
    StoreLock::SetBlockingWait(nullptr);
    EXPECT_EQ(g_blockingWaits, 1);
}

}
}
