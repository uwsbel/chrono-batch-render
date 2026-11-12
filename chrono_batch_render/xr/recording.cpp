// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/xr/recording.h"
#include "chrono_batch_render/util/profiler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <type_traits>

#include "chrono_batch_render/util/error.h"

namespace chrono::batch_render::xr {
namespace {

constexpr char kMagic[8] = {'C', 'B', 'R', 'X', 'R', 'E', 'C', '1'};

static_assert(std::is_trivially_copyable_v<RecordedInput>,
              "a recorded frame is written as its bytes");

template <class T> void Write(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <class T> T Read(std::ifstream& in, const std::filesystem::path& path) {
    T value{};
    if (!in.read(reinterpret_cast<char*>(&value), sizeof(T))) {
        throw Error("{} ends early", path.string());
    }
    return value;
}

}

void Recording::Add(const Frame& frame) {
    CHRONO_BATCH_RENDER_ZONE("XR/Record");
    if (!m_start) {
        m_start = frame.displayTime;
    }
    RecordedInput input;
    input.time = double(frame.displayTime - *m_start) * 1e-9;
    input.head = frame.head;
    input.gazed = frame.gaze.has_value();
    input.gaze = frame.gaze.value_or(Pose{});
    input.controllers = frame.controllers;
    for (Controller& controller : input.controllers) {
        controller.pose = controller.rawPose;
        controller.aim = controller.rawAim;
    }
    input.hands = frame.hands;
    input.body = frame.body;
    input.face = frame.face;
    m_inputs.push_back(input);
    for (uint32_t hand = 0; hand < 2; ++hand) {
        if (!frame.profiles[hand].empty()) {
            m_profiles[hand] = frame.profiles[hand];
        }
    }
}

void Recording::Clear() {
    m_inputs.clear();
    m_profiles = {};
    m_start.reset();
}

double Recording::Duration() const {
    return m_inputs.empty() ? 0 : m_inputs.back().time;
}

void Recording::Save(const std::filesystem::path& path) const {
    CHRONO_BATCH_RENDER_ZONE("XR/Save recording");
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw Error("Couldn't write the XR recording {}", path.string());
    }
    out.write(kMagic, sizeof(kMagic));
    Write(out, uint32_t(sizeof(RecordedInput)));
    for (const std::string& profile : m_profiles) {
        Write(out, uint32_t(profile.size()));
        out.write(profile.data(), std::streamsize(profile.size()));
    }
    Write(out, uint64_t(m_inputs.size()));
    out.write(reinterpret_cast<const char*>(m_inputs.data()),
              std::streamsize(m_inputs.size() * sizeof(RecordedInput)));
    if (!out) {
        throw Error("Couldn't write the XR recording {}", path.string());
    }
}

Recording Recording::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Error("Couldn't open the XR recording {}", path.string());
    }
    char magic[sizeof(kMagic)] = {};
    if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        throw Error("{} isn't an XR recording", path.string());
    }
    if (Read<uint32_t>(in, path) != sizeof(RecordedInput)) {
        throw Error("{} was recorded by a build that lays frames out differently", path.string());
    }
    Recording recording;
    for (std::string& profile : recording.m_profiles) {
        profile.resize(Read<uint32_t>(in, path));
        if (!in.read(profile.data(), std::streamsize(profile.size()))) {
            throw Error("{} ends early", path.string());
        }
    }
    recording.m_inputs.resize(Read<uint64_t>(in, path));
    if (!in.read(reinterpret_cast<char*>(recording.m_inputs.data()),
                 std::streamsize(recording.m_inputs.size() * sizeof(RecordedInput)))) {
        throw Error("{} ends early", path.string());
    }
    return recording;
}

std::function<void(uint64_t, double, SimulatedInput&)> Recording::Script(bool loop) const {
    auto inputs = std::make_shared<const std::vector<RecordedInput>>(m_inputs);
    const std::string profile = !m_profiles[1].empty() ? m_profiles[1] : m_profiles[0];
    auto start = std::make_shared<std::optional<double>>();
    return [inputs, profile, start, loop](uint64_t, double time, SimulatedInput& input) {
        if (inputs->empty()) {
            return;
        }
        if (!*start) {
            *start = time;
        }
        double elapsed = time - **start;
        const double length = inputs->back().time;
        if (loop && length > 0) {
            elapsed = std::fmod(elapsed, length);
        }
        auto after = std::ranges::lower_bound(*inputs, elapsed, {}, &RecordedInput::time);
        if (after == inputs->end() ||
            (after != inputs->begin() && elapsed - (after - 1)->time < after->time - elapsed)) {
            --after;
        }
        const RecordedInput& at = *after;
        input.head = at.head;
        input.gaze = at.gazed ? std::optional<Pose>(at.gaze) : std::nullopt;
        input.controllers = at.controllers;
        input.hands = at.hands;
        input.body = at.body;
        input.face = at.face;
        if (!profile.empty()) {
            input.profile = profile;
        }
    };
}

}
