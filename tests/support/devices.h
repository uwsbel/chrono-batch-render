// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/device/adapter.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/device/vulkan/api.h"
#include "chrono_batch_render/util/error.h"

namespace chrono::batch_render::test {

inline bool EnvironmentFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) == "1";
}

class TestDevices : public ::testing::Environment {
  public:
    static std::vector<Ref<device::Device>>& Get() {
        static std::vector<Ref<device::Device>> devices = Create();
        return devices;
    }

    void SetUp() override {
        if (EnvironmentFlag("CHRONO_BATCH_RENDER_TEST_REQUIRE_DEVICE") && Get().empty()) {
            FAIL() << "CHRONO_BATCH_RENDER_TEST_REQUIRE_DEVICE is set, but no supported adapter "
                      "was found";
        }
    }

    void TearDown() override {
        Get().clear();
        EXPECT_EQ(device::vulkan::ValidationMessageCount(), 0u)
            << "the Vulkan validation layer reported errors or warnings, logged above";
    }

  private:
    static std::vector<Ref<device::Device>> Create() {
        std::vector<Ref<device::Device>> devices;
        if (!EnvironmentFlag("CHRONO_BATCH_RENDER_TEST_FULL")) {
            try {
                devices.push_back(device::Device::Create());
            } catch (const UnsupportedError&) {
            }
            return devices;
        }

        for (const device::AdapterInfo& adapter : device::EnumerateAdapters()) {
            if (adapter.unsupported.empty()) {
                devices.push_back(device::Device::Create({.adapter = adapter.alias}));
            }
        }
        return devices;
    }
};

inline ::testing::Environment* const kTestDevices =
    ::testing::AddGlobalTestEnvironment(new TestDevices);

template <class Test> void ForEachDevice(Test test) {
    if (TestDevices::Get().empty()) {
        GTEST_SKIP() << "no supported adapter";
    }
    for (const Ref<device::Device>& device : TestDevices::Get()) {
        const device::AdapterInfo& adapter = device->Adapter();
        SCOPED_TRACE(adapter.alias + " (" + adapter.name + ")");
        test(*device);
        device->GetQueue().WaitIdle();
    }
}

}
