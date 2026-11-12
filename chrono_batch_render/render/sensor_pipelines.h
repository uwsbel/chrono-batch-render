// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <memory>

#include "chrono_batch_render/device/commands.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/device/program.h"
#include "chrono_batch_render/render/kernels/sensor_pipeline/pipeline.h"
#include "chrono_batch_render/sensor_pipeline/codegen.h"

namespace chrono::batch_render {

struct CompiledSensorPipeline {
    std::shared_ptr<const sensor_pipeline::SensorPipelineDefinition> definition;
    sensor_pipeline::GeneratedSensorPipeline generated;
    Ref<device::Program> program;
};

std::shared_ptr<const CompiledSensorPipeline>
CompileSensorPipeline(device::Device& device,
                      std::shared_ptr<const sensor_pipeline::SensorPipelineDefinition> definition,
                      const sensor_pipeline::NodeRegistry& registry);

void RecordSensorPipeline(device::CommandEncoder& encoder, const CompiledSensorPipeline& compiled,
                          const SensorPipelineParams& params, uint32_t captures, uint64_t elements);

}
