// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/scene/tables/procedural.h"

#include <string>

#include "chrono_batch_render/bvh/kernels/bvh.h"
#include "chrono_batch_render/device/device.h"

namespace chrono::batch_render {

void ProceduralBlas::Allocate(device::Device& device, bvh::Builder& builder, uint32_t count,
                              std::string_view label) {
    capacity = count;
    blas = nullptr;
    boxes = nullptr;
    fresh = true;
    if (count == 0) {
        return;
    }
    boxes = device.CreateBuffer({.bytes = uint64_t(count) * sizeof(BvhBox),
                                 .usage = device::BufferUsage::AccelBuildInput,
                                 .label = std::string(label)});
    blas = builder.CreateBlas(
        device::AccelBoxes{.boxes = boxes->Slice(0), .count = count, .refittable = true});
}

void ProceduralBlas::Bound(ColumnStore& store) {
    if (bounds.count == 0) {
        bounds = store.Arrays(ColumnType::Float3Array).Allocate(2);
    }
}

void ProceduralBlas::Release(ColumnStore& store) {
    if (bounds.count > 0) {
        store.Arrays(ColumnType::Float3Array).Free(bounds);
        bounds = {};
    }
}

bool ProceduralBlas::Rebuilds(uint32_t maxRefits) {
    if (fresh || refits >= maxRefits) {
        fresh = false;
        refits = 0;
        return true;
    }
    ++refits;
    return false;
}

void ProceduralBlas::Hold(std::vector<Ref<const RefCounted>>& held) const {
    if (blas) {
        held.emplace_back(blas);
        held.emplace_back(boxes);
    }
}

}
