// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <dlpack/dlpack.h>

#include "chrono_batch_render/device/operation.h"
#include "chrono_batch_render/device/stream.h"
#include "chrono_batch_render/device/tensor.h"
#include "chrono_batch_render/export.h"
#include "chrono_batch_render/stage/schema/schema.h"
#include "chrono_batch_render/util/span.h"

namespace chrono::batch_render {

class Structure;

enum class QuaternionOrder : uint8_t { Xyzw, Wxyz };

class UntypedAttribute;
class ViewState;
struct PrimBlock;
template <class T> class Attribute;
template <class T> class ArrayAttribute;

class CHRONO_BATCH_RENDER_API View {
  public:
    View() = default;
    View(Structure& structure, SchemaId schema, std::vector<uint32_t> prims);
    View(Structure& structure, SchemaId schema, std::vector<uint32_t> prims,
         std::string_view instance);
    View(Structure& structure, SchemaId schema, const PrimBlock& block, uint32_t index = 0);

    uint32_t PrimCount() const;
    SchemaId Schema() const;
    bool Dense(SchemaId table) const;
    Span<const uint32_t> Prims() const;
    View World(uint32_t world) const;

    ColumnRef Column(std::string_view field) const;
    UntypedAttribute Get(ColumnRef column) const;
    UntypedAttribute Get(std::string_view field) const;
    template <class T> Attribute<T> Get(ColumnRef column) const;
    template <class T> ArrayAttribute<T> GetArray(ColumnRef column) const;
    template <class T> auto Get(Field<T> field) const;

    template <class T> Attribute<T> Get(std::string_view field) const {
        return Get<T>(Column(field));
    }

    template <class T> ArrayAttribute<T> GetArray(std::string_view field) const {
        return GetArray<T>(Column(field));
    }

    device::Operation WritePoses(const DLTensor& source, Stream producer = Stream::None(),
                                 QuaternionOrder order = QuaternionOrder::Xyzw,
                                 const DLTensor* rows = nullptr,
                                 const DLTensor* mask = nullptr) const;
    device::Operation WritePoses(const DLTensor& translation, const DLTensor& rotation,
                                 Stream producer = Stream::None(),
                                 QuaternionOrder order = QuaternionOrder::Xyzw,
                                 const DLTensor* rows = nullptr,
                                 const DLTensor* mask = nullptr) const;
    device::Operation WritePoses(const device::Tensor& source, Stream producer = Stream::None(),
                                 QuaternionOrder order = QuaternionOrder::Xyzw,
                                 const device::Tensor& rows = {},
                                 const device::Tensor& mask = {}) const;
    device::Operation WritePoses(const device::Tensor& translation, const device::Tensor& rotation,
                                 Stream producer = Stream::None(),
                                 QuaternionOrder order = QuaternionOrder::Xyzw,
                                 const device::Tensor& rows = {},
                                 const device::Tensor& mask = {}) const;
    device::Tensor ReadPoses(device::DType type = device::DType::Float32,
                             Stream consumer = Stream::Host(),
                             QuaternionOrder order = QuaternionOrder::Xyzw) const;

    device::Operation WriteWorldPoses(const DLTensor& source, Stream producer = Stream::None(),
                                      QuaternionOrder order = QuaternionOrder::Xyzw,
                                      const DLTensor* rows = nullptr,
                                      const DLTensor* mask = nullptr) const;
    device::Operation WriteWorldPoses(const DLTensor& translation, const DLTensor& rotation,
                                      Stream producer = Stream::None(),
                                      QuaternionOrder order = QuaternionOrder::Xyzw,
                                      const DLTensor* rows = nullptr,
                                      const DLTensor* mask = nullptr) const;
    device::Operation WriteWorldPoses(const device::Tensor& source,
                                      Stream producer = Stream::None(),
                                      QuaternionOrder order = QuaternionOrder::Xyzw,
                                      const device::Tensor& rows = {},
                                      const device::Tensor& mask = {}) const;
    device::Operation
    WriteWorldPoses(const device::Tensor& translation, const device::Tensor& rotation,
                    Stream producer = Stream::None(), QuaternionOrder order = QuaternionOrder::Xyzw,
                    const device::Tensor& rows = {}, const device::Tensor& mask = {}) const;
    device::Tensor ReadWorldPoses(device::DType type = device::DType::Float32,
                                  Stream consumer = Stream::Host(),
                                  QuaternionOrder order = QuaternionOrder::Xyzw) const;
    device::Tensor ReadWorldScales(device::DType type = device::DType::Float32,
                                   Stream consumer = Stream::Host()) const;

    const std::shared_ptr<ViewState>& State() const {
        return m_state;
    }

  private:
    std::shared_ptr<ViewState> m_state;
};

}
