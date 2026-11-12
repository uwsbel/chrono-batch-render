// SPDX-License-Identifier: BSD-3-Clause
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/queue.h"
#include "support/devices.h"
#include "support/threads.h"

namespace chrono::batch_render {
namespace {

using device::Buffer;
using device::BufferRange;
using device::CommandEncoder;
using device::Device;
using device::Download;
using device::Operation;
using device::Queue;
using test::ForEachDevice;
using test::RunTogether;
using test::WithinDeadline;

constexpr auto kDeadline = std::chrono::seconds(180);

std::vector<uint32_t> Pattern(size_t words, uint32_t seed) {
    std::vector<uint32_t> values(words);
    for (size_t at = 0; at < words; ++at) {
        values[at] = seed * 2654435761u + uint32_t(at);
    }
    return values;
}

std::vector<uint32_t> ReadBack(Queue& queue, BufferRange range) {
    const Download download = queue.ReadAsync(range);
    download.Wait();
    const std::span<const std::byte> bytes = download.Bytes();
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

TEST(QueueThreadsTest, EveryThreadReadsBackWhatItWrote) {
    ForEachDevice([](Device& device) {
        Queue& queue = device.GetQueue();
        static constexpr size_t kThreads = 8;
        static constexpr int kRounds = 120;
        static constexpr size_t kSmallWords = 64;
        static constexpr size_t kLargeWords = 16 * 1024;
        std::atomic<uint64_t> checked = 0;
        WithinDeadline(kDeadline, "QueueThreadsTest.EveryThreadReadsBackWhatItWrote", [&] {
            RunTogether(kThreads, [&](size_t thread) {
                const Ref<Buffer> target = device.CreateBuffer(
                    {.bytes = kLargeWords * sizeof(uint32_t), .label = "queue threads"});
                std::mt19937 random(uint32_t(thread) + 1);
                for (int round = 0; round < kRounds; ++round) {
                    const uint32_t seed = uint32_t(thread << 20) | uint32_t(round);
                    const int path = int(random() % 6);
                    const size_t words = path == 0 ? kSmallWords : kLargeWords;
                    const BufferRange range = target->Slice(0, words * sizeof(uint32_t));
                    std::vector<uint32_t> expected = Pattern(words, seed);
                    if (path <= 1) {
                        queue.Write(range, expected.data());
                    } else if (path == 2) {
                        const BufferRange staged =
                            queue.Upload(expected.data(), words * sizeof(uint32_t));
                        CommandEncoder encoder = queue.CreateEncoder();
                        encoder.Copy(staged, range);
                        queue.Submit(encoder.Finish());
                    } else if (path == 3) {
                        CommandEncoder encoder = queue.CreateEncoder();
                        encoder.Fill(range, seed);
                        queue.Submit(encoder.Finish());
                        expected.assign(words, seed);
                    } else if (path == 4) {
                        queue.Write(range, expected.data());
                        queue.Mark().Wait();
                    } else {
                        queue.Write(range, expected.data());
                        const Operation marked = queue.Mark();
                        while (!marked.Ready()) {
                            std::this_thread::yield();
                        }
                    }
                    ASSERT_EQ(ReadBack(queue, range), expected)
                        << "thread " << thread << " round " << round << " path " << path;
                    ++checked;
                }
            });
            queue.WaitIdle();
        });
        EXPECT_EQ(checked, kThreads * kRounds);
    });
}

TEST(QueueThreadsTest, UploadsSurviveOtherThreadsSubmissions) {
    ForEachDevice([](Device& device) {
        Queue& queue = device.GetQueue();
        constexpr size_t kWords = 12 * 1024 * 1024;
        constexpr uint64_t kBytes = kWords * sizeof(uint32_t);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const std::vector<uint32_t> expected = Pattern(kWords, 77 + uint32_t(attempt));
            const std::vector<uint32_t> junk = Pattern(kWords, 999);
            const Ref<Buffer> mine = device.CreateBuffer({.bytes = kBytes, .label = "mine"});
            const Ref<Buffer> theirs = device.CreateBuffer({.bytes = kBytes, .label = "theirs"});
            std::mutex mutex;
            std::condition_variable turn;
            int step = 0;
            const auto await = [&](int wanted) {
                std::unique_lock lock(mutex);
                turn.wait(lock, [&] { return step >= wanted; });
            };
            const auto advance = [&](int to) {
                {
                    const std::lock_guard lock(mutex);
                    step = to;
                }
                turn.notify_all();
            };
            WithinDeadline(kDeadline, "QueueThreadsTest.UploadsSurviveOtherThreadsSubmissions",
                           [&] {
                               RunTogether({[&] {
                                                const BufferRange staged =
                                                    queue.Upload(expected.data(), kBytes);
                                                advance(1);
                                                await(2);
                                                CommandEncoder encoder = queue.CreateEncoder();
                                                encoder.Copy(staged, mine->Slice(0, kBytes));
                                                queue.Submit(encoder.Finish());
                                            },
                                            [&] {
                                                await(1);
                                                for (int churn = 0; churn < 4; ++churn) {
                                                    const BufferRange staged =
                                                        queue.Upload(junk.data(), kBytes);
                                                    CommandEncoder encoder = queue.CreateEncoder();
                                                    encoder.Copy(staged, theirs->Slice(0, kBytes));
                                                    queue.Submit(encoder.Finish()).Wait();
                                                }
                                                advance(2);
                                            }});
                               queue.WaitIdle();
                           });
            EXPECT_EQ(ReadBack(queue, mine->Slice(0, kBytes)), expected)
                << "attempt " << attempt << ": another thread's work reused an upload";
        }
    });
}

TEST(QueueThreadsTest, BatchesOnSeveralThreadsKeepEverySubmission) {
    ForEachDevice([](Device& device) {
        Queue& queue = device.GetQueue();
        static constexpr size_t kThreads = 6;
        static constexpr int kRounds = 80;
        WithinDeadline(
            kDeadline, "QueueThreadsTest.BatchesOnSeveralThreadsKeepEverySubmission", [&] {
                RunTogether(kThreads, [&](size_t thread) {
                    const Ref<Buffer> target =
                        device.CreateBuffer({.bytes = 4096, .label = "batched"});
                    for (int round = 0; round < kRounds; ++round) {
                        const uint32_t value = uint32_t(thread * 1000 + round);
                        queue.BeginBatch();
                        CommandEncoder encoder = queue.CreateEncoder();
                        encoder.Fill(target->Slice(0, 4096), value);
                        const Operation filled = queue.Submit(encoder.Finish());
                        const Operation ended = queue.EndBatch();
                        if (round % 2 == 0) {
                            filled.Wait();
                        } else {
                            ended.Wait();
                        }
                        const std::vector<uint32_t> words = ReadBack(queue, target->Slice(0, 4096));
                        ASSERT_EQ(words, std::vector<uint32_t>(1024, value))
                            << "thread " << thread << " round " << round;
                    }
                });
                queue.WaitIdle();
            });
    });
}

TEST(QueueThreadsTest, RetiredObjectsAreFreedExactlyOnce) {
    ForEachDevice([](Device& device) {
        Queue& queue = device.GetQueue();
        static constexpr size_t kThreads = 8;
        static constexpr int kRounds = 200;
        std::atomic<int> freed = 0;
        WithinDeadline(kDeadline, "QueueThreadsTest.RetiredObjectsAreFreedExactlyOnce", [&] {
            RunTogether(kThreads, [&](size_t) {
                for (int round = 0; round < kRounds; ++round) {
                    const Operation marked = queue.Mark();
                    queue.Retire(std::shared_ptr<int>(new int(round),
                                                      [&](int* held) {
                                                          ++freed;
                                                          delete held;
                                                      }),
                                 marked);
                    if (round % 7 == 0) {
                        marked.Wait();
                    }
                }
            });
            queue.WaitIdle();
            queue.Completed();
        });
        EXPECT_EQ(freed, int(kThreads) * kRounds);
    });
}

}
}
