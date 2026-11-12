// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <optional>

#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/stage/store_lock.h"
#include "chrono_batch_render/stage/types.h"

namespace chrono::batch_render {

class SceneImpl;
struct SimulationClock;

struct UpdateDesc {
    std::optional<Ordinal> ordinal;
    bool wait = false;
    bool finish = false;
};

class SceneUpdate {
  public:
    SceneUpdate(SceneImpl& scene, StoreLock::Guard lock, device::Operation published);
    ~SceneUpdate();
    SceneUpdate(const SceneUpdate&) = delete;
    SceneUpdate& operator=(const SceneUpdate&) = delete;
    SceneUpdate(SceneUpdate&&) = delete;
    SceneUpdate& operator=(SceneUpdate&&) = delete;

    const device::Operation& Published() const {
        return m_published;
    }

    void Refresh(Stream producer = Stream::None());

    const Snapshot& Tables();
    const SimulationClock& Clock() const;

  private:
    StoreLock::Guard m_lock;
    SceneImpl& m_scene;
    device::Operation m_published;
    std::optional<Snapshot> m_snapshot;
};

}
