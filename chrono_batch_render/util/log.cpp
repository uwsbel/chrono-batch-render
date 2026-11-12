// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/util/log.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "chrono_batch_render/util/environment.h"
#include "chrono_batch_render/util/error.h"

namespace chrono::batch_render {
namespace {

constexpr Choice<LogLevel> kLevels[] = {{"debug", LogLevel::Debug},
                                        {"info", LogLevel::Info},
                                        {"warning", LogLevel::Warning},
                                        {"error", LogLevel::Error},
                                        {"off", LogLevel::Off}};

LogLevel ReadLevel() {
    try {
        return EnvironmentChoice("CHRONO_BATCH_RENDER_LOG", kLevels).value_or(LogLevel::Info);
    } catch (const Error& error) {
        std::fprintf(stderr, "chrono-batch-render: warning: %s\n", error.what());
        return LogLevel::Info;
    }
}

const char* Prefix(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "chrono-batch-render: debug: ";
    case LogLevel::Warning:
        return "chrono-batch-render: warning: ";
    case LogLevel::Error:
        return "chrono-batch-render: error: ";
    default:
        return "chrono-batch-render: ";
    }
}

struct LogState {
    std::atomic<LogLevel> level{ReadLevel()};
    std::mutex mutex;
    LogSink sink;
};

LogState& State() {
    static LogState state;
    return state;
}

}

LogLevel GetLogLevel() {
    return State().level.load(std::memory_order_relaxed);
}

void SetLogLevel(LogLevel level) {
    State().level.store(level, std::memory_order_relaxed);
}

void SetLogSink(LogSink sink) {
    LogState& state = State();
    std::lock_guard lock(state.mutex);
    state.sink = std::move(sink);
}

void LogMessage(LogLevel level, std::string_view message) {
    if (level == LogLevel::Off || level < GetLogLevel()) {
        return;
    }

    LogState& state = State();
    std::lock_guard lock(state.mutex);
    if (state.sink) {
        state.sink(level, message);
        return;
    }
    std::fprintf(stderr, "%s%.*s\n", Prefix(level), int(message.size()), message.data());
}

}
