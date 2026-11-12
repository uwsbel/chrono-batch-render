// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/util/version.h"

#include <fmt/format.h>

#include "build_info.h"

namespace chrono::batch_render {

std::string Version::ToString() const {
    if (patch != 0) {
        return fmt::format("{}.{}.{}", major, minor, patch);
    }
    return fmt::format("{}.{}", major, minor);
}

const BuildInfo& GetBuildInfo() {
    static const BuildInfo info{{CHRONO_BATCH_RENDER_VERSION_MAJOR,
                                 CHRONO_BATCH_RENDER_VERSION_MINOR,
                                 CHRONO_BATCH_RENDER_VERSION_PATCH},
                                CHRONO_BATCH_RENDER_GIT_SHA,
                                CHRONO_BATCH_RENDER_GIT_DIRTY,
                                CHRONO_BATCH_RENDER_COMMIT_DATE,
                                CHRONO_BATCH_RENDER_BUILD_DATE,
                                CHRONO_BATCH_RENDER_VERSION_FULL};
    return info;
}

}
