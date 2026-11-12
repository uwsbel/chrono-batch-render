// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "chrono_batch_render/xr/frame.h"
#include "chrono_batch_render/xr/session.h"
#include "chrono_batch_render/xr_export.h"

namespace chrono::batch_render::xr {

struct RecordedInput {
    double time = 0;
    Pose head;
    bool gazed = false;
    Pose gaze;
    std::array<Controller, 2> controllers;
    std::array<HandJoints, 2> hands;
    BodyJoints body;
    FaceExpressions face;
};

class CHRONO_BATCH_RENDER_XR_API Recording {
  public:
    void Add(const Frame& frame);
    void Clear();

    void Save(const std::filesystem::path& path) const;
    static Recording Load(const std::filesystem::path& path);

    size_t Size() const {
        return m_inputs.size();
    }

    double Duration() const;

    const std::vector<RecordedInput>& Inputs() const {
        return m_inputs;
    }

    std::function<void(uint64_t, double, SimulatedInput&)> Script(bool loop = false) const;

  private:
    std::vector<RecordedInput> m_inputs;
    std::array<std::string, 2> m_profiles;
    std::optional<int64_t> m_start;
};

}
