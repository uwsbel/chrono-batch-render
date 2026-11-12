// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <memory>

#include "chrono_batch_render/import/importer.h"

namespace chrono::batch_render {

std::shared_ptr<Importer> CreateObjImporter();

}
