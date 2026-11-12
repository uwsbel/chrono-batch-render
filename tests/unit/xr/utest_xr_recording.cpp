// SPDX-License-Identifier: BSD-3-Clause
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/xr/recording.h"
#include "chrono_batch_render/xr/session.h"

namespace chrono::batch_render::xr {
namespace {

std::filesystem::path Scratch(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

TEST(RecordingTest, PlaysBackWhatItRecordedThroughTheSimulatedHeadset) {
    SessionDesc desc;
    desc.backend = Backend::Simulated;
    desc.simulated.placeControllers = true;
    desc.simulated.script = [](uint64_t frame, double, SimulatedInput& input) {
        input.head.position = {0.1 * double(frame), 0, 1.6};
        if (frame >= 3) {
            input.Press(Side::Right, "trigger/value", 1);
        }
    };
    Session session(desc);
    Recording recording;
    std::vector<double> heads;
    std::vector<float> triggers;
    for (int k = 0; k < 6; ++k) {
        const Frame frame = session.BeginFrame();
        recording.Add(frame);
        heads.push_back(frame.head.position.x);
        triggers.push_back(frame.Controller(Side::Right).trigger);
    }
    ASSERT_EQ(recording.Size(), 6u);
    EXPECT_GT(recording.Duration(), 0);
    const std::filesystem::path path = Scratch("chrono_batch_render_xr_recording.cbrxr");
    recording.Save(path);
    const Recording loaded = Recording::Load(path);
    std::filesystem::remove(path);
    ASSERT_EQ(loaded.Size(), recording.Size());

    SessionDesc replay;
    replay.backend = Backend::Simulated;
    replay.simulated.script = loaded.Script();
    Session played(replay);
    for (size_t k = 0; k < heads.size(); ++k) {
        const Frame frame = played.BeginFrame();
        EXPECT_NEAR(frame.head.position.x, heads[k], 1e-9) << "frame " << k;
        EXPECT_FLOAT_EQ(frame.Controller(Side::Right).trigger, triggers[k]) << "frame " << k;
        EXPECT_EQ(frame.profiles[1], "/interaction_profiles/oculus/touch_controller");
    }
}

TEST(RecordingTest, RefusesFilesThatArentRecordings) {
    const std::filesystem::path path = Scratch("chrono_batch_render_not_a_recording.cbrxr");
    std::ofstream(path) << "hello";
    EXPECT_THROW(Recording::Load(path), Error);
    std::filesystem::remove(path);
    EXPECT_THROW(Recording::Load(path), Error);
}

}
}
