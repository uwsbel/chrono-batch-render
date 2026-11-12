// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/stage/views/view.h"
#include "chrono_batch_render/stage/views/view_state.h"

#include <set>
#include <utility>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/stage/views/attribute.h"
#include "chrono_batch_render/stage/views/poses.h"
#include "chrono_batch_render/util/error.h"
#include "chrono_batch_render/util/span.h"

namespace chrono::batch_render {

ViewState::ViewState(Structure& structure, SchemaId schema, std::vector<uint32_t> prims,
                     uint32_t instance)
    : m_structure(structure), m_schema(schema), m_instance(instance), m_prims(std::move(prims)) {
    const chrono::batch_render::Prims& records = structure.GetPrims();
    m_generations.reserve(m_prims.size());
    for (const uint32_t prim : m_prims) {
        const PrimRecord& record = records.Record(prim);
        if (schema != SchemaId::None && !IsA(record.schema, schema) &&
            !HasApi(record.apis, schema)) {
            throw Error("Prim {}, a {}, isn't a {}", prim, Info(record.schema).name,
                        Info(schema).name);
        }
        m_generations.push_back(records.Generation(prim));
    }
}

bool ViewState::Live(uint32_t index) const {
    const chrono::batch_render::Prims& records = m_structure.GetPrims();
    return records.Alive(m_prims[index]) &&
           records.Generation(m_prims[index]) == m_generations[index];
}

const ViewRows& ViewState::Rows(SchemaId table) {
    ViewRows& rows = m_rows[table];
    if (rows.epoch == m_structure.Epoch()) {
        return rows;
    }
    const chrono::batch_render::Prims& records = m_structure.GetPrims();
    rows = {};
    rows.epoch = m_structure.Epoch();
    rows.rows.reserve(m_prims.size());
    std::set<uint32_t> worlds;
    for (uint32_t index = 0; index < m_prims.size(); ++index) {
        if (!Live(index)) {
            rows.rows.push_back(kNoRow);
            continue;
        }
        uint32_t row = kNoRow;
        if (!IsMultipleApi(table)) {
            row = records.Row(m_prims[index], table);
        } else if (m_instance == kNoInstance) {
            throw Error("{} is applied many times, so a view reaches it through an instance name",
                        Info(table).name);
        } else {
            row = records.InstanceRow(m_prims[index], table, m_instance);
            if (row == kNoRow) {
                throw Error("{} has no {} {}", m_structure.GetPaths().Path(m_prims[index]),
                            Info(table).name, m_structure.GetTokens().Text(Token{m_instance}));
            }
        }
        const uint32_t world = records.Record(m_prims[index]).world;
        rows.rows.push_back(row);
        ++rows.live;
        worlds.insert(world);
        ViewBlock* last = rows.blocks.empty() ? nullptr : &rows.blocks.back();
        if (last && last->first + last->count == index && last->firstRow + last->count == row) {
            ++last->count;
        } else {
            rows.blocks.push_back({index, row, 1});
        }
    }
    rows.worlds.assign(worlds.begin(), worlds.end());
    return rows;
}

const Ref<device::Buffer>& ViewState::DeviceRows(SchemaId table) {
    Rows(table);
    ViewRows& rows = m_rows.at(table);
    if (!rows.device && !rows.rows.empty()) {
        rows.device = GetColumnStore().GetDevice().CreateBuffer(
            {.label = "view rows"}, std::span<const uint32_t>(rows.rows));
    }
    return rows.device;
}

const Ref<device::Buffer>& ViewState::XformParents() {
    if (m_xformParents && m_xformParentsEpoch == m_structure.Epoch()) {
        return m_xformParents;
    }
    const chrono::batch_render::Prims& prims = m_structure.GetPrims();
    std::vector<uint32_t> parents;
    parents.reserve(std::max<size_t>(m_prims.size(), 1));
    for (uint32_t index = 0; index < m_prims.size(); ++index) {
        uint32_t up = Live(index) ? prims.Record(m_prims[index]).parent : kNoPrim;
        while (up != kNoPrim && !IsA(prims.Record(up).schema, SchemaId::Xformable)) {
            up = prims.Record(up).parent;
        }
        parents.push_back(up == kNoPrim ? kNoRow : prims.Row(up, SchemaId::Xformable));
    }
    if (parents.empty()) {
        parents.push_back(kNoRow);
    }
    m_xformParents = GetColumnStore().GetDevice().CreateBuffer({.label = "view parents"},
                                                               std::span<const uint32_t>(parents));
    m_xformParentsEpoch = m_structure.Epoch();
    return m_xformParents;
}

RowSource ViewState::Source(SchemaId table) {
    const ViewRows& rows = Rows(table);
    if (rows.Dense()) {
        return {0, rows.blocks.front().firstRow};
    }
    const Ref<device::Buffer>& device = DeviceRows(table);
    return {device ? device->Address() : 0, 0};
}

View::View(Structure& structure, SchemaId schema, std::vector<uint32_t> prims)
    : m_state(std::make_shared<ViewState>(structure, schema, std::move(prims))) {}

View::View(Structure& structure, SchemaId schema, std::vector<uint32_t> prims,
           std::string_view instance)
    : m_state(std::make_shared<ViewState>(structure, schema, std::move(prims),
                                          structure.GetTokens().Intern(instance).id)) {}

View::View(Structure& structure, SchemaId schema, const PrimBlock& block, uint32_t index) {
    std::vector<uint32_t> prims;
    prims.reserve(block.copies);
    for (uint32_t copy = 0; copy < block.copies; ++copy) {
        prims.push_back(block.Prim(copy, index));
    }
    m_state = std::make_shared<ViewState>(structure, schema, std::move(prims));
}

uint32_t View::PrimCount() const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    return m_state ? m_state->Size() : 0;
}

SchemaId View::Schema() const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    return m_state ? m_state->Schema() : SchemaId::None;
}

bool View::Dense(SchemaId table) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    return m_state && m_state->Rows(table).Dense();
}

Span<const uint32_t> View::Prims() const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    return m_state ? m_state->Prims() : std::span<const uint32_t>{};
}

View View::World(uint32_t world) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    std::vector<uint32_t> prims;
    if (m_state) {
        const chrono::batch_render::Prims& records = m_state->GetStructure().GetPrims();
        for (uint32_t index = 0; index < m_state->Size(); ++index) {
            if (m_state->Live(index) && records.Record(m_state->Prims()[index]).world == world) {
                prims.push_back(m_state->Prims()[index]);
            }
        }
    }
    if (!m_state) {
        return View();
    }
    View narrowed;
    narrowed.m_state = std::make_shared<ViewState>(m_state->GetStructure(), m_state->Schema(),
                                                   std::move(prims), m_state->Instance());
    return narrowed;
}

device::Operation View::WritePoses(const DLTensor& source, Stream producer, QuaternionOrder order,
                                   const DLTensor* rows, const DLTensor* mask) const {
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).Write(source, producer, order, rows, mask);
}

device::Operation View::WritePoses(const DLTensor& translation, const DLTensor& rotation,
                                   Stream producer, QuaternionOrder order, const DLTensor* rows,
                                   const DLTensor* mask) const {
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).Write(translation, rotation, producer, order, rows, mask);
}

device::Operation View::WritePoses(const device::Tensor& source, Stream producer,
                                   QuaternionOrder order, const device::Tensor& rows,
                                   const device::Tensor& mask) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).Write(source, producer, order, rows, mask);
}

device::Operation View::WritePoses(const device::Tensor& translation,
                                   const device::Tensor& rotation, Stream producer,
                                   QuaternionOrder order, const device::Tensor& rows,
                                   const device::Tensor& mask) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).Write(translation, rotation, producer, order, rows, mask);
}

device::Tensor View::ReadPoses(device::DType type, Stream consumer, QuaternionOrder order) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).Read(type, consumer, order);
}

device::Operation View::WriteWorldPoses(const DLTensor& source, Stream producer,
                                        QuaternionOrder order, const DLTensor* rows,
                                        const DLTensor* mask) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    device::Device& device = m_state->GetColumnStore().GetDevice();
    return Poses(m_state).WriteWorld(
        device.Import(source, producer, "world pose staging"), nullptr, producer, order,
        rows ? device.Import(*rows, producer, "pose rows") : device::Tensor(),
        mask ? device.Import(*mask, producer, "pose mask") : device::Tensor());
}

device::Operation View::WriteWorldPoses(const DLTensor& translation, const DLTensor& rotation,
                                        Stream producer, QuaternionOrder order,
                                        const DLTensor* rows, const DLTensor* mask) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    device::Device& device = m_state->GetColumnStore().GetDevice();
    const device::Tensor rotations = device.Import(rotation, producer, "world rotation staging");
    return Poses(m_state).WriteWorld(
        device.Import(translation, producer, "world translation staging"), &rotations, producer,
        order, rows ? device.Import(*rows, producer, "pose rows") : device::Tensor(),
        mask ? device.Import(*mask, producer, "pose mask") : device::Tensor());
}

device::Operation View::WriteWorldPoses(const device::Tensor& source, Stream producer,
                                        QuaternionOrder order, const device::Tensor& rows,
                                        const device::Tensor& mask) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).WriteWorld(source, nullptr, producer, order, rows, mask);
}

device::Operation View::WriteWorldPoses(const device::Tensor& translation,
                                        const device::Tensor& rotation, Stream producer,
                                        QuaternionOrder order, const device::Tensor& rows,
                                        const device::Tensor& mask) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).WriteWorld(translation, &rotation, producer, order, rows, mask);
}

device::Tensor View::ReadWorldPoses(device::DType type, Stream consumer,
                                    QuaternionOrder order) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).ReadWorld(type, consumer, order);
}

device::Tensor View::ReadWorldScales(device::DType type, Stream consumer) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no poses");
    }
    return Poses(m_state).ReadWorldScales(type, consumer);
}

ColumnRef View::Column(std::string_view field) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no attributes");
    }
    const chrono::batch_render::Prims& prims = m_state->GetStructure().GetPrims();
    if (m_state->Schema() != SchemaId::None) {
        uint64_t shared = ~uint64_t(0);
        for (const uint32_t prim : m_state->Prims()) {
            if (prims.Alive(prim)) {
                shared &= prims.Record(prim).apis;
            }
        }
        if (const ColumnRef column = FindColumn(m_state->Schema(), shared, field)) {
            return column;
        }
        throw Error("{} has no attribute {}", Info(m_state->Schema()).name, field);
    }
    for (const uint32_t prim : m_state->Prims()) {
        if (!prims.Alive(prim)) {
            continue;
        }
        if (const ColumnRef column =
                FindColumn(prims.Record(prim).schema, prims.Record(prim).apis, field)) {
            return column;
        }
    }
    throw Error("No prim of the view has an attribute {}", field);
}

UntypedAttribute View::Get(std::string_view field) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    return Get(Column(field));
}

UntypedAttribute View::Get(ColumnRef column) const {
    const auto lock = LockStore(m_state ? m_state->GetColumnStore().Lock() : nullptr);
    if (!m_state) {
        throw Error("An empty view has no attributes");
    }
    return UntypedAttribute(m_state, column);
}

}
