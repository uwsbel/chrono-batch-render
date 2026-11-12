// SPDX-License-Identifier: BSD-3-Clause
#include <string>

#include <gtest/gtest.h>

#include "chrono_batch_render/util/library.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {
namespace {

TEST(RoctxTest, ZonesPushAndPopRangesWhenAProfilerHasLoadedRoctx) {
    const Library roctx = Library::Find({"libroctx64.so"});
    if (!roctx) {
        GTEST_SKIP() << "runs with the stand-in libroctx64.so preloaded";
    }
    long (*pushes)() = nullptr;
    long (*pops)() = nullptr;
    const char* (*last)() = nullptr;
    ASSERT_TRUE(roctx.Symbol("FakeRoctxPushes", pushes));
    ASSERT_TRUE(roctx.Symbol("FakeRoctxPops", pops));
    ASSERT_TRUE(roctx.Symbol("FakeRoctxLastRange", last));
    const long pushed = pushes();
    const long popped = pops();
    {
        CHRONO_BATCH_RENDER_ZONE("Roctx/Test");
        EXPECT_EQ(pushes(), pushed + 1);
        EXPECT_EQ(std::string(last()), "chrono:Roctx/Test");
        EXPECT_EQ(pops(), popped);
    }
    EXPECT_EQ(pops(), popped + 1);
}

}
}
