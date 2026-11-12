// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chrono::batch_render::test {

inline void WithinDeadline(std::chrono::seconds limit, const std::string& what,
                           const std::function<void()>& work) {
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!finished.wait_for(lock, limit, [&] { return done; })) {
            std::fprintf(stderr, "%s didn't finish within %lld s, so it is hung or deadlocked\n",
                         what.c_str(), static_cast<long long>(limit.count()));
            std::fflush(stderr);
            std::abort();
        }
    });
    const auto finish = [&] {
        {
            const std::lock_guard lock(mutex);
            done = true;
        }
        finished.notify_all();
        watchdog.join();
    };
    try {
        work();
    } catch (...) {
        finish();
        throw;
    }
    finish();
}

inline void RunTogether(const std::vector<std::function<void()>>& tasks) {
    std::mutex mutex;
    std::condition_variable started;
    size_t ready = 0;
    bool go = false;
    std::exception_ptr failure;
    std::vector<std::thread> threads;
    threads.reserve(tasks.size());
    for (const std::function<void()>& task : tasks) {
        threads.emplace_back([&, task] {
            {
                std::unique_lock lock(mutex);
                ++ready;
                started.notify_all();
                started.wait(lock, [&] { return go; });
            }
            try {
                task();
            } catch (...) {
                const std::lock_guard lock(mutex);
                if (!failure) {
                    failure = std::current_exception();
                }
            }
        });
    }
    {
        std::unique_lock lock(mutex);
        started.wait(lock, [&] { return ready == tasks.size(); });
        go = true;
    }
    started.notify_all();
    for (std::thread& thread : threads) {
        thread.join();
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

inline void RunTogether(size_t count, const std::function<void(size_t)>& task) {
    std::vector<std::function<void()>> tasks;
    for (size_t index = 0; index < count; ++index) {
        tasks.emplace_back([task, index] { task(index); });
    }
    RunTogether(tasks);
}

}
