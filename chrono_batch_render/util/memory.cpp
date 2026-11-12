// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/util/memory.h"

#if defined(_WIN32)
#include <windows.h>

#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>

#include <unistd.h>
#endif

namespace chrono::batch_render {

std::optional<uint64_t> ResidentBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return uint64_t(counters.WorkingSetSize);
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &count) == KERN_SUCCESS) {
        return uint64_t(info.resident_size);
    }
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    uint64_t size = 0;
    uint64_t resident = 0;
    if (statm >> size >> resident) {
        return resident * uint64_t(sysconf(_SC_PAGESIZE));
    }
#endif
    return std::nullopt;
}

}
