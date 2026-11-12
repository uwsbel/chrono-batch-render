// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/stage/store_lock.h"

#include <exception>
#include <utility>

#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render {

namespace {

std::atomic<StoreLock::BlockingWait> g_blockingWait{nullptr};

}

void StoreLock::SetBlockingWait(BlockingWait blockingWait) {
    g_blockingWait.store(blockingWait, std::memory_order_release);
}

StoreLock::Guard::Guard(const StoreLock& lock) : m_lock(&lock) {
    if (!lock.m_mutex.try_lock()) {
        const BlockingWait blockingWait = g_blockingWait.load(std::memory_order_acquire);
        const auto wait = [](void* mutex) { static_cast<std::recursive_mutex*>(mutex)->lock(); };
        if (blockingWait) {
            blockingWait(wait, &lock.m_mutex);
        } else {
            wait(&lock.m_mutex);
        }
    }
    Enter();
}

StoreLock::Guard::Guard(const StoreLock& lock, std::try_to_lock_t) {
    if (lock.m_mutex.try_lock()) {
        m_lock = &lock;
        Enter();
    }
}

StoreLock::Guard::~Guard() {
    Leave();
}

StoreLock::Guard::Guard(Guard&& other) noexcept : m_lock(std::exchange(other.m_lock, nullptr)) {}

StoreLock::Guard& StoreLock::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        Leave();
        m_lock = std::exchange(other.m_lock, nullptr);
    }
    return *this;
}

void StoreLock::Guard::Enter() {
    if (m_lock->m_depth++ == 0) {
        m_lock->m_owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
}

void StoreLock::Guard::Leave() noexcept {
    if (m_lock == nullptr) {
        return;
    }
    const StoreLock& lock = *std::exchange(m_lock, nullptr);
    const bool outermost = lock.m_depth == 1;
    if (outermost) {
        lock.RunDeferred();
    }
    if (--lock.m_depth == 0) {
        lock.m_owner.store(std::thread::id(), std::memory_order_relaxed);
    }
    lock.m_mutex.unlock();
    while (outermost && lock.HasDeferred() && lock.m_mutex.try_lock()) {
        lock.m_depth = 1;
        lock.m_owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
        lock.RunDeferred();
        lock.m_depth = 0;
        lock.m_owner.store(std::thread::id(), std::memory_order_relaxed);
        lock.m_mutex.unlock();
    }
}

std::optional<StoreLock::Guard> StoreLock::TryLock() const {
    Guard guard(*this, std::try_to_lock);
    if (!guard.OwnsLock()) {
        return std::nullopt;
    }
    return guard;
}

void StoreLock::Defer(std::function<void()> write) const {
    {
        const std::lock_guard lock(m_deferredMutex);
        m_deferred.push_back(std::move(write));
        m_hasDeferred.store(true, std::memory_order_release);
    }
    [[maybe_unused]] const std::optional<Guard> guard = TryLock();
}

bool StoreLock::CanDefer() const {
    const std::lock_guard lock(m_deferredMutex);
    return m_deferred.size() < kMaxDeferred;
}

void StoreLock::DropDeferred() const {
    const std::lock_guard lock(m_deferredMutex);
    m_deferred.clear();
    m_hasDeferred.store(false, std::memory_order_release);
}

void StoreLock::RunDeferred() const {
    while (HasDeferred()) {
        std::vector<std::function<void()>> writes;
        {
            const std::lock_guard lock(m_deferredMutex);
            writes.swap(m_deferred);
            m_hasDeferred.store(false, std::memory_order_release);
        }
        for (const std::function<void()>& write : writes) {
            try {
                write();
            } catch (const std::exception& error) {
                Log(LogLevel::Warning, "a deferred scene write failed: {}", error.what());
            }
        }
    }
}

void StoreLock::Check(const char* what) const {
    if (!HeldByThisThread()) {
        throw Error("{} without holding the scene's lock; every public scene call must take it",
                    what);
    }
}

}
