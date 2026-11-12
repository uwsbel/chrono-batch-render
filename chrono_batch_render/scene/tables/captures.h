// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>

#include "chrono_batch_render/scene/tables/products.h"

namespace chrono::batch_render {

struct SimulationClock {
    double start = 0;
    double end = 0;
    double resetTime = 0;
    uint64_t steps = 0;
    uint64_t resets = 0;
};

class CaptureSchedule {
  public:
    explicit CaptureSchedule(Scene& scene) : m_scene(scene) {}

    void Schedule(std::span<const std::shared_ptr<const ResolvedProduct>> products, double start,
                  double end);
    void Reset();
    bool Due(uint32_t prim) const;

  private:
    struct Entry {
        float frameRate = 0;
        float startTime = 0;
        int64_t next = 0;
        bool due = true;
    };

    Scene& m_scene;
    std::map<uint32_t, Entry> m_entries;
};

}
