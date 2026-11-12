// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/stage/views/poses.h"

#include <utility>

#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/scene/schema/columns.h"
#include "chrono_batch_render/stage/columns/column_store.h"
#include "chrono_batch_render/stage/columns/kernels/cells.h"
#include "chrono_batch_render/stage/views/layout.h"
#include "chrono_batch_render/stage/views/staged.h"
#include "chrono_batch_render/stage/views/view_state.h"
#include "chrono_batch_render/stage/views/world_poses.h"
#include "chrono_batch_render/util/profiler.h"

namespace chrono::batch_render {

namespace {

uint32_t SourceRows(const DLTensor& tensor, int64_t width, const char* what) {
    const bool lanes = tensor.ndim == 1 && tensor.dtype.lanes == width;
    const bool wide =
        tensor.ndim >= 2 && tensor.shape[tensor.ndim - 1] == width && tensor.dtype.lanes == 1;
    if (!wide && !lanes) {
        throw Error("{} are written from [..., {}] tensors", what, width);
    }
    if (tensor.dtype.code != kDLFloat || (tensor.dtype.bits != 32 && tensor.dtype.bits != 64)) {
        throw Error("{} are float32 or float64, not {}-bit code {}", what, tensor.dtype.bits,
                    tensor.dtype.code);
    }
    if (tensor.strides != nullptr) {
        int64_t expected = 1;
        for (int dim = tensor.ndim - 1; dim >= 0; --dim) {
            if (tensor.shape[dim] != 1 && tensor.strides[dim] != expected) {
                throw UnsupportedError("{} are written from contiguous tensors only", what);
            }
            expected *= tensor.shape[dim];
        }
    }
    uint64_t rows = 1;
    for (int dim = 0; dim < tensor.ndim - (lanes ? 0 : 1); ++dim) {
        rows *= uint64_t(tensor.shape[dim]);
    }
    return uint32_t(rows);
}

void CheckOwn(device::Device& device, const device::Tensor& tensor, const char* what) {
    if (&tensor.GetDevice() != &device) {
        throw Error("{} are on {}, not the scene's {}; Device::Copy or Device::Import moves them",
                    what, tensor.GetDevice().Adapter().alias, device.Adapter().alias);
    }
    const Span<const int64_t> shape = tensor.Shape();
    const Span<const int64_t> strides = tensor.Strides();
    int64_t packed = 1;
    for (size_t axis = shape.size(); axis-- > 0;) {
        if (!strides.empty() && shape[axis] > 1 && strides[axis] != packed) {
            throw UnsupportedError("{} are written from contiguous tensors only", what);
        }
        packed *= shape[axis];
    }
}

uint32_t TensorRows(device::Device& device, const device::Tensor& tensor, int64_t width,
                    const char* what) {
    const Span<const int64_t> shape = tensor.Shape();
    if (shape.size() < 2 || shape.back() != width) {
        throw Error("{} are written from [..., {}] tensors", what, width);
    }
    if (tensor.Type() != device::DType::Float32 && tensor.Type() != device::DType::Float64) {
        throw Error("{} are float32 or float64", what);
    }
    CheckOwn(device, tensor, what);
    uint64_t rows = 1;
    for (size_t dim = 0; dim + 1 < shape.size(); ++dim) {
        rows *= uint64_t(shape[dim]);
    }
    return uint32_t(rows);
}

}

Poses::Poses(std::shared_ptr<ViewState> view)
    : m_translate(view, kXformableTranslate), m_orient(std::move(view), kXformableOrient) {}

device::Operation Poses::Write(const DLTensor& source, Stream producer, QuaternionOrder order,
                               const DLTensor* rows, const DLTensor* mask) {
    SourceRows(source, kPoseWords, "Poses");
    device::Device& device = m_translate.GetColumnStore().GetDevice();
    const StoreLock* store = m_translate.GetColumnStore().Lock();
    std::optional<StoreLock::Guard> lock =
        store ? store->TryLock() : std::optional<StoreLock::Guard>(std::in_place);
    if (!lock) {
        std::optional<StagedTensor> values;
        std::optional<StagedTensor> stagedRows;
        std::optional<StagedTensor> stagedMask;
        if (store->CanDefer() && StageOptional(device, &source, producer, values) &&
            StageOptional(device, rows, producer, stagedRows) &&
            StageOptional(device, mask, producer, stagedMask)) {
            store->Defer([poses = *this, values = std::move(*values), order,
                          stagedRows = std::move(stagedRows),
                          stagedMask = std::move(stagedMask)]() mutable {
                DLTensor rowView;
                DLTensor maskView;
                const DLTensor written = values.View();
                poses.Write(written, values.Producer(), order, ViewOptional(stagedRows, rowView),
                            ViewOptional(stagedMask, maskView));
            });
            return {};
        }
        lock = store->Lock();
    }
    return Write(device.Import(source, producer, "pose staging"), producer, order,
                 rows ? device.Import(*rows, producer, "pose rows") : device::Tensor(),
                 mask ? device.Import(*mask, producer, "pose mask") : device::Tensor());
}

device::Operation Poses::Write(const DLTensor& translation, const DLTensor& rotation,
                               Stream producer, QuaternionOrder order, const DLTensor* rows,
                               const DLTensor* mask) {
    if (SourceRows(translation, 3, "Translations") != SourceRows(rotation, 4, "Rotations") ||
        translation.dtype.bits != rotation.dtype.bits) {
        throw Error("Translations and rotations need as many rows and the same precision");
    }
    device::Device& device = m_translate.GetColumnStore().GetDevice();
    const StoreLock* store = m_translate.GetColumnStore().Lock();
    std::optional<StoreLock::Guard> lock =
        store ? store->TryLock() : std::optional<StoreLock::Guard>(std::in_place);
    if (!lock) {
        std::optional<StagedTensor> translations;
        std::optional<StagedTensor> rotations;
        std::optional<StagedTensor> stagedRows;
        std::optional<StagedTensor> stagedMask;
        if (store->CanDefer() && StageOptional(device, &translation, producer, translations) &&
            StageOptional(device, &rotation, producer, rotations) &&
            StageOptional(device, rows, producer, stagedRows) &&
            StageOptional(device, mask, producer, stagedMask)) {
            store->Defer([poses = *this, translations = std::move(*translations),
                          rotations = std::move(*rotations), order,
                          stagedRows = std::move(stagedRows),
                          stagedMask = std::move(stagedMask)]() mutable {
                DLTensor rowView;
                DLTensor maskView;
                const DLTensor moved = translations.View();
                const DLTensor turned = rotations.View();
                poses.Write(moved, turned, translations.Producer(), order,
                            ViewOptional(stagedRows, rowView), ViewOptional(stagedMask, maskView));
            });
            return {};
        }
        lock = store->Lock();
    }
    return Write(device.Import(translation, producer, "translation staging"),
                 device.Import(rotation, producer, "rotation staging"), producer, order,
                 rows ? device.Import(*rows, producer, "pose rows") : device::Tensor(),
                 mask ? device.Import(*mask, producer, "pose mask") : device::Tensor());
}

device::Operation Poses::Write(const device::Tensor& source, Stream producer, QuaternionOrder order,
                               const device::Tensor& rows, const device::Tensor& mask) {
    return Dispatch(source, nullptr, producer, order, rows, mask);
}

device::Operation Poses::Write(const device::Tensor& translation, const device::Tensor& rotation,
                               Stream producer, QuaternionOrder order, const device::Tensor& rows,
                               const device::Tensor& mask) {
    device::Device& device = m_translate.GetColumnStore().GetDevice();
    if (TensorRows(device, translation, 3, "Translations") !=
            TensorRows(device, rotation, 4, "Rotations") ||
        translation.Type() != rotation.Type()) {
        throw Error("Translations and rotations need as many rows and the same precision");
    }
    return Dispatch(translation, &rotation, producer, order, rows, mask);
}

void Poses::CheckSources(const device::Tensor& values, const device::Tensor* rotations,
                         const device::Tensor& rows) const {
    device::Device& device = m_translate.GetColumnStore().GetDevice();
    const uint32_t width = rotations ? 3 : kPoseWords;
    const uint32_t sourceRows = TensorRows(device, values, width, "Poses");
    if (rotations && (TensorRows(device, *rotations, 4, "Rotations") != sourceRows ||
                      rotations->Type() != values.Type())) {
        throw Error("Translations and rotations need as many rows and the same precision");
    }
    if (!rows.Empty()) {
        const bool indices = rows.Shape().size() == 1 && (rows.Type() == device::DType::Int32 ||
                                                          rows.Type() == device::DType::UInt32);
        if (!indices || uint32_t(rows.Shape()[0]) != Size()) {
            throw Error("Pose rows are a contiguous [{}] int32 or uint32 tensor, a source row per "
                        "prim",
                        Size());
        }
        CheckOwn(device, rows, "Pose rows");
    } else if (sourceRows != Size()) {
        throw Error("A tensor of {} poses for {} prims", sourceRows, Size());
    }
}

device::Operation Poses::Dispatch(const device::Tensor& values, const device::Tensor* rotations,
                                  Stream producer, QuaternionOrder order,
                                  const device::Tensor& rows, const device::Tensor& mask) {
    const auto lock = LockStore(m_translate.GetColumnStore().Lock());
    CHRONO_BATCH_RENDER_ZONE("Scene/Write");
    if (Size() == 0) {
        return {};
    }
    ColumnStore& store = m_translate.GetColumnStore();
    device::Device& device = store.GetDevice();
    CheckSources(values, rotations, rows);
    const UntypedAttribute::Mask selected = m_translate.Upload(mask);
    const bool doubles = values.Type() == device::DType::Float64;
    store.Flush();
    device.GetQueue().Follow(producer);
    PoseParams params{};
    params.translateStamps = store.Stamps(kXformableTranslate);
    params.orientStamps = store.Stamps(kXformableOrient);
    params.translate.address = store.Schema(SchemaId::Xformable).Pages(kXformableTranslate.index);
    params.orient.address = store.Schema(SchemaId::Xformable).Pages(kXformableOrient.index);
    const RowSource rowSource = m_translate.m_view->Source(SchemaId::Xformable);
    params.rows.address = rowSource.rows;
    params.firstRow = rowSource.firstRow;
    params.values.address = values.Address();
    params.rotations.address = rotations ? rotations->Address() : 0;
    params.sources.address = rows.Empty() ? 0 : rows.Address();
    params.mask.address = selected.address;
    params.count = Size();
    params.doubles = doubles ? 1 : 0;
    params.order = uint32_t(order);
    store.Touch(kXformableTranslate);
    store.Touch(kXformableOrient);
    m_translate.StampWorlds();
    const device::Operation written = store.Dispatch("WritePoses", params, params.count);
    store.Reassert(kXformableTranslate, m_translate.Rows().rows);
    store.Reassert(kXformableOrient, m_translate.Rows().rows);
    return written;
}

device::Tensor Poses::Read(device::DType type, Stream consumer, QuaternionOrder order) const {
    const auto lock = LockStore(m_translate.GetColumnStore().Lock());
    CHRONO_BATCH_RENDER_ZONE("Scene/Read");
    if (type != device::DType::Float32 && type != device::DType::Float64) {
        throw Error("Poses are gathered as float32 or float64");
    }
    ColumnStore& store = m_translate.GetColumnStore();
    device::Device& device = store.GetDevice();
    const bool doubles = type == device::DType::Float64;
    store.Flush();
    const Ref<device::Buffer> values = device.CreateBuffer(
        {.bytes = std::max<uint64_t>(uint64_t(Size()) * kPoseWords * (doubles ? 8 : 4), 4),
         .usage = device::BufferUsage::Interop,
         .label = "gathered poses"});
    PoseParams params{};
    params.translate.address = store.Schema(SchemaId::Xformable).Pages(kXformableTranslate.index);
    params.orient.address = store.Schema(SchemaId::Xformable).Pages(kXformableOrient.index);
    const RowSource rowSource = m_translate.m_view->Source(SchemaId::Xformable);
    params.rows.address = rowSource.rows;
    params.firstRow = rowSource.firstRow;
    params.values.address = values->Address();
    params.count = Size();
    params.doubles = doubles ? 1 : 0;
    params.order = uint32_t(order);
    store.Dispatch("ReadPoses", params, params.count).Wait(consumer);
    return device::Tensor(values, 0, type, {int64_t(Size()), int64_t(kPoseWords)});
}

class Batch {
  public:
    explicit Batch(device::Queue& queue) : m_queue(&queue) {
        m_queue->BeginBatch();
    }

    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

    ~Batch() {
        if (m_queue != nullptr) {
            m_queue->EndBatch();
        }
    }

    device::Operation End() {
        return std::exchange(m_queue, nullptr)->EndBatch();
    }

  private:
    device::Queue* m_queue = nullptr;
};

static WorldPoseSource& WorldPoses(const ViewState& view) {
    WorldPoseSource* source = view.GetStructure().GetWorldPoses();
    if (source == nullptr) {
        throw UnsupportedError("World poses need a scene, which composes the hierarchy");
    }
    return *source;
}

device::Operation Poses::WriteWorld(const device::Tensor& values, const device::Tensor* rotations,
                                    Stream producer, QuaternionOrder order,
                                    const device::Tensor& rows, const device::Tensor& mask) {
    const auto lock = LockStore(m_translate.GetColumnStore().Lock());
    CHRONO_BATCH_RENDER_ZONE("Scene/WriteWorld");
    CheckSources(values, rotations, rows);
    if (Size() == 0) {
        return {};
    }
    device::Queue& queue = m_translate.GetColumnStore().GetDevice().GetQueue();
    queue.Follow(producer);
    Batch batch(queue);
    const device::Tensor local = WorldPoses(*m_translate.m_view)
                                     .Localize(*m_translate.m_view, values, rotations, order, rows);
    Dispatch(local, nullptr, Stream::None(), QuaternionOrder::Xyzw, {}, mask);
    return batch.End();
}

device::Tensor Poses::ReadWorld(device::DType type, Stream consumer, QuaternionOrder order) const {
    const auto lock = LockStore(m_translate.GetColumnStore().Lock());
    CHRONO_BATCH_RENDER_ZONE("Scene/ReadWorld");
    if (type != device::DType::Float32 && type != device::DType::Float64) {
        throw Error("World poses are gathered as float32 or float64");
    }
    const Batch batch(m_translate.GetColumnStore().GetDevice().GetQueue());
    return WorldPoses(*m_translate.m_view).Read(*m_translate.m_view, type, consumer, order);
}

device::Tensor Poses::ReadWorldScales(device::DType type, Stream consumer) const {
    const auto lock = LockStore(m_translate.GetColumnStore().Lock());
    CHRONO_BATCH_RENDER_ZONE("Scene/ReadWorldScales");
    if (type != device::DType::Float32 && type != device::DType::Float64) {
        throw Error("World scales are gathered as float32 or float64");
    }
    const Batch batch(m_translate.GetColumnStore().GetDevice().GetQueue());
    return WorldPoses(*m_translate.m_view).ReadScales(*m_translate.m_view, type, consumer);
}

}
