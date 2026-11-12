// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/scene/tables/captures.h"

#include <algorithm>
#include <cmath>

#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render {

namespace {

constexpr double kCaptureTolerance = 1e-6;

}

void CaptureSchedule::Schedule(std::span<const std::shared_ptr<const ResolvedProduct>> products,
                               double start, double end) {
    const double dt = end - start;
    for (const std::shared_ptr<const ResolvedProduct>& product : products) {
        Entry& entry = m_entries[product->prim];
        const float rate = product->frameRate;
        if (rate <= 0 || dt <= 0) {
            entry.due = true;
            continue;
        }
        const float first = product->startTime;
        if (entry.frameRate != rate || entry.startTime != first) {
            const double next = std::ceil((start - first) * rate - kCaptureTolerance);
            entry = {rate, first, std::max<int64_t>(0, int64_t(next))};
        }
        const double reached = (end - first) * rate + kCaptureTolerance;
        entry.due = double(entry.next) <= reached;
        if (!entry.due) {
            continue;
        }
        const auto last = int64_t(std::floor(reached));
        if (last > entry.next) {
            Log(LogLevel::Debug, "{} skipped {} in a step of {} s",
                m_scene.ByIndex(product->prim).Path(),
                Counted(uint64_t(last - entry.next), "capture"), dt);
        }
        entry.next = last + 1;
    }
}

void CaptureSchedule::Reset() {
    m_entries.clear();
}

bool CaptureSchedule::Due(uint32_t prim) const {
    const auto found = m_entries.find(prim);
    return found == m_entries.end() || found->second.due;
}

}
