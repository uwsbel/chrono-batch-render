// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "chrono_batch_render/bvh/builder.h"
#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/ref.h"
#include "chrono_batch_render/scene/tables/storage.h"
#include "chrono_batch_render/scene/tables/table.h"
#include "chrono_batch_render/stage/columns/column_store.h"
#include "chrono_batch_render/stage/publish/change_set.h"
#include "chrono_batch_render/stage/structure/structure.h"

namespace chrono::batch_render {

struct ProceduralBlas {
    uint32_t capacity = 0;
    uint32_t refits = 0;
    bool fresh = false;
    Ref<bvh::Blas> blas;
    Ref<device::Buffer> boxes;
    ArrayRef bounds{};

    void Allocate(device::Device& device, bvh::Builder& builder, uint32_t count,
                  std::string_view label);
    void Bound(ColumnStore& store);
    void Release(ColumnStore& store);
    bool Rebuilds(uint32_t maxRefits);
    void Hold(std::vector<Ref<const RefCounted>>& held) const;
};

template <class Entry, class Place, class Forget>
void TrackPrims(const ChangeSet& changes, const Prims& prims, SchemaId schema,
                const std::unordered_map<uint32_t, Entry>& entries, Place place, Forget forget) {
    const auto matches = [&](uint32_t prim) {
        return prims.Alive(prim) && IsA(prims.Record(prim).schema, schema);
    };
    if (changes.resync) {
        while (!entries.empty()) {
            forget(entries.begin()->first);
        }
        for (uint32_t prim = 0; prim < prims.Size(); ++prim) {
            if (matches(prim)) {
                place(prim);
            }
        }
        return;
    }
    for (const StructureEntry& entry : changes.removed) {
        const auto found = entries.find(entry.prim);
        if (found != entries.end() && found->second.generation == entry.generation) {
            forget(entry.prim);
        }
    }
    for (const StructureEntry& entry : changes.added) {
        if (matches(entry.prim) && prims.Generation(entry.prim) == entry.generation) {
            if (entries.contains(entry.prim)) {
                forget(entry.prim);
            }
            place(entry.prim);
        }
    }
}

template <class Record>
void PublishRecords(RecordTable<Record>& records, ColumnStore& store, SchemaId schema,
                    std::vector<uint32_t>& relinked, std::span<const uint32_t> updated,
                    TableVersions& versions, TableId table, TableId moved) {
    records.Reserve(store.Schema(schema).Capacity());
    if (records.Upload(store.GetDevice())) {
        relinked = {kGlobalWorld};
    }
    if (!relinked.empty()) {
        versions.Record(table, relinked);
    }
    if (!updated.empty()) {
        versions.Record(moved, updated);
    }
    relinked.clear();
}

}
