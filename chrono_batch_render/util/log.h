// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#ifdef CHRONO_BATCH_RENDER_FMT
#include <fmt/format.h>
#endif

#include "chrono_batch_render/export.h"

namespace chrono::batch_render {

enum class LogLevel : int { Debug, Info, Warning, Error, Off };

using LogSink = std::function<void(LogLevel level, std::string_view message)>;

CHRONO_BATCH_RENDER_API LogLevel GetLogLevel();

CHRONO_BATCH_RENDER_API void SetLogLevel(LogLevel level);

CHRONO_BATCH_RENDER_API void SetLogSink(LogSink sink);

CHRONO_BATCH_RENDER_API void LogMessage(LogLevel level, std::string_view message);

#ifdef CHRONO_BATCH_RENDER_FMT
template <class... Args>
void Log(LogLevel level, fmt::format_string<Args...> format, Args&&... args) {
    if (level >= GetLogLevel()) {
        LogMessage(level, fmt::format(format, std::forward<Args>(args)...));
    }
}

inline std::string Counted(uint64_t count, std::string_view noun) {
    return fmt::format("{} {}{}", count, noun, count == 1 ? "" : "s");
}
#endif

}
