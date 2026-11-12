// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "chrono_batch_render/export.h"

namespace chrono::batch_render {

class CHRONO_BATCH_RENDER_API StoreLock {
  public:
    class CHRONO_BATCH_RENDER_API Guard {
      public:
        Guard() = default;
        explicit Guard(const StoreLock& lock);
        Guard(const StoreLock& lock, std::try_to_lock_t);
        ~Guard();
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept;
        Guard& operator=(Guard&& other) noexcept;

        bool OwnsLock() const {
            return m_lock != nullptr;
        }

      private:
        void Enter();
        void Leave() noexcept;

        const StoreLock* m_lock = nullptr;
    };

    StoreLock() = default;
    StoreLock(const StoreLock&) = delete;
    StoreLock& operator=(const StoreLock&) = delete;

    [[nodiscard]] Guard Lock() const {
        return Guard(*this);
    }

    [[nodiscard]] std::optional<Guard> TryLock() const;

    bool HeldByThisThread() const {
        return m_owner.load(std::memory_order_relaxed) == std::this_thread::get_id();
    }

    void Check(const char* what) const;

    void Defer(std::function<void()> write) const;

    bool CanDefer() const;

    static constexpr size_t kMaxDeferred = 256;

    void DropDeferred() const;

    using BlockingWait = void (*)(void (*wait)(void* context), void* context);
    static void SetBlockingWait(BlockingWait blockingWait);

  private:
    bool HasDeferred() const {
        return m_hasDeferred.load(std::memory_order_acquire);
    }

    void RunDeferred() const;

    mutable std::recursive_mutex m_mutex;
    mutable std::atomic<std::thread::id> m_owner{};
    mutable uint32_t m_depth = 0;
    mutable std::mutex m_deferredMutex;
    mutable std::vector<std::function<void()>> m_deferred;
    mutable std::atomic<bool> m_hasDeferred{false};
};

[[nodiscard]] inline StoreLock::Guard LockStore(const StoreLock* lock) {
    return lock ? lock->Lock() : StoreLock::Guard();
}

inline void CheckStoreLock(const StoreLock* lock, const char* what) {
    if (lock) {
        lock->Check(what);
    }
}

}
