// SPDX-License-Identifier: BSD-3-Clause
#include <string>
#include <vector>

#include "chrono_batch_render/app/cli.h"

int main(int argc, char** argv) {
    return chrono::batch_render::app::RunCli(std::vector<std::string>(argv + 1, argv + argc));
}
