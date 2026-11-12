// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "chrono_batch_render/algorithms/scan.h"
#include "chrono_batch_render/device/buffer.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/device/queue.h"
#include "chrono_batch_render/material/codegen.h"
#include "chrono_batch_render/overlay/overlay_impl.h"
#include "chrono_batch_render/render/kernels/render/render.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/render/sensor_pipelines.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/scene/tables/captures.h"
#include "chrono_batch_render/scene/tables/plans.h"
#include "chrono_batch_render/scene/tables/sensor_pipelines.h"

namespace chrono::batch_render {

class RendererImpl {
  public:
    explicit RendererImpl(Ref<Scene> scene, const RendererDesc& desc = {});

    RenderOutput Step(std::span<const std::string> products, Stream stream = Stream::None());

    struct OwnedVar {
        uint32_t product = 0;
        std::string renderVar;
        device::Tensor tensor;
        std::string channel;
        std::string output;
    };

    struct OwnedCloud {
        uint32_t product = kNoPrim;
        uint32_t slots = 0;
        uint32_t index = 0;
        std::string output;
        bool world = false;
        Ref<device::Buffer> cloud;
        Ref<device::Buffer> records;
        Ref<device::Buffer> flags;
        Ref<device::Buffer> offsets;
    };

    struct OwnedOverlay {
        uint32_t product = 0;
        device::Tensor tensor;
        uint64_t depth = 0;
        Ref<device::Buffer> ownDepth;
        Ref<device::Buffer> primIds;
    };

    struct OutputSet {
        std::array<std::vector<Capture>, kRenderEntryCount> captures;
        std::array<Ref<device::Buffer>, kRenderEntryCount> captureBuffers;
        std::vector<OwnedVar> vars;
        std::vector<OwnedOverlay> overlays;
        std::vector<OwnedCloud> clouds;
        std::map<uint32_t, uint64_t> rates;
        std::vector<Ref<device::Buffer>> rateBuffers;
        Ref<device::Buffer> arena;
        bool outlined = false;
    };

    struct SensorPipelineRun {
        const PlannedSensorPipeline* planned = nullptr;
        std::shared_ptr<const SensorPipelineInstance> instance;
        std::shared_ptr<const CompiledSensorPipeline> compiled;
        std::vector<uint32_t> products;
    };

    struct SensorPipelineState {
        uint32_t definition = 0;
        uint32_t captures = 0;
        Ref<device::Buffer> state;
    };

    struct StepPlan {
        std::shared_ptr<const ProductPlan> plan;
        std::optional<OutputSet> set;
    };

    StepPlan& PlanProducts(std::span<const std::string> products);

    OutputSet& AcquireSet(StepPlan& plan);
    uint64_t AcquireCloud(OutputSet& set, uint32_t index, const std::string& path,
                          const ResolvedProduct& product, const ResolvedVar& var, uint64_t next);
    device::Operation Trace(const ProductPlan& plan, OutputSet& set, const std::set<uint32_t>& due,
                            const Snapshot& snapshot, std::span<const SensorPipelineRun> runs);
    std::vector<SensorPipelineRun> PrepareRuns(const ProductPlan& plan,
                                               const std::set<uint32_t>& due);
    void RecordRuns(device::CommandEncoder& encoder, const ProductPlan& plan, OutputSet& set,
                    std::span<const SensorPipelineRun> runs, const Snapshot& snapshot);

    Ref<Scene> m_scene;
    uint64_t m_maxPixelsPerLaunch;
    Streaming m_streaming = Streaming::Blocking;
    Ref<Overlays> m_overlays;
    Ref<device::Program> CreateRenderProgram(const material::MaterialDefinitionSet& definitions,
                                             std::vector<std::string> entries) const;
    uint64_t ProgramHash(const material::MaterialDefinitionSet& definitions) const;
    void UpdateRenderProgram();
    bool WaitsForKernels() const;
    device::Kernel& RenderEntry(std::string_view name);

    Ref<device::Program> m_renderProgram;
    uint64_t m_renderHash = 0;
    Ref<device::Program> m_pendingProgram;
    material::MaterialDefinitionSetRef m_pendingDefinitions;
    uint64_t m_pendingHash = 0;
    std::chrono::steady_clock::time_point m_pendingStart;
    std::set<std::string, std::less<>> m_usedEntries;
    uint64_t m_failedHash = 0;

    std::unique_ptr<algorithms::Scan> m_scan;
    uint32_t m_scanCapacity = 0;
    std::list<StepPlan> m_plans;

    std::map<uint32_t, std::shared_ptr<const CompiledSensorPipeline>> m_pipelinePrograms;
    std::set<uint32_t> m_pendingPipelines;
    std::set<uint32_t> m_failedPipelines;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, SensorPipelineState>
        m_pipelineStates;

    CaptureSchedule m_captures;
    double m_lastTime = 0;
    uint64_t m_lastResets = 0;

    std::mutex m_stepMutex;
    std::optional<OverlayPass> m_overlayPass;
};

}
