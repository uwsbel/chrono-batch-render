// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "chrono_batch_render/algorithms/kernels.h"
#include "chrono_batch_render/bvh/kernels.h"
#include "chrono_batch_render/device/compute/runtime_compiler.h"
#include "chrono_batch_render/device/cuda/api.h"
#include "chrono_batch_render/device/cuda/device.h"
#include "chrono_batch_render/device/hip/api.h"
#include "chrono_batch_render/device/hip/device.h"
#include "chrono_batch_render/device/slang/compiler.h"
#include "chrono_batch_render/io/kernels.h"
#include "chrono_batch_render/material/codegen.h"
#include "chrono_batch_render/overlay/kernels.h"
#include "chrono_batch_render/render/kernels.h"
#include "chrono_batch_render/scene/kernels.h"
#include "chrono_batch_render/texture/kernels.h"
#include "chrono_batch_render/util/disk_cache.h"
#include "chrono_batch_render/util/error.h"

namespace chrono::batch_render::device {
namespace {

struct RuntimeProgram {
    const KernelLibrary* library;
    std::string module;
    std::vector<const char*> entries;
};

std::vector<RuntimeProgram> RuntimePrograms() {
    return {
        {&kAlgorithmsKernels,
         "chrono_batch_render.algorithms.kernels.radix_sort",
         {"CountDigits", "Scatter"}},
        {&kAlgorithmsKernels,
         "chrono_batch_render.algorithms.kernels.scan",
         {"SumTiles", "ScanTiles"}},
        {&kBvhKernels,
         "chrono_batch_render.bvh.kernels.instances",
         {"MaskInstances", "GatherWorlds"}},
        {&kBvhKernels,
         "chrono_batch_render.bvh.kernels.lbvh",
         {"Prepare", "Morton", "Hierarchy", "FitBoxes", "Seed", "Advance", "Collapse", "Finish"}},
        {&kBvhKernels, "chrono_batch_render.bvh.kernels.refit", {"RefitLeaves", "RefitNodes"}},
        {&kIoKernels,
         "chrono_batch_render.io.kernels.colorize",
         {"ScalarRange", "ColorizeScalars", "ColorizeHdr", "ColorizeNormals", "ColorizeIds"}},
        {&kOverlayKernels,
         "chrono_batch_render.overlay.kernels.project",
         {"ProjectOverlays", "ScatterOverlays", "FindOverlayTiles"}},
        {&kOverlayKernels,
         "chrono_batch_render.overlay.kernels.groups",
         {"KeyOverlayRows", "StartOverlayGroups", "MeasureOverlayGroups"}},
        {&kOverlayKernels,
         "chrono_batch_render.overlay.kernels.composite",
         {"FoldOverlayTiles", "CompositeOverlays", "BlendOverlay"}},
        {&kOverlayKernels,
         "chrono_batch_render.overlay.kernels.selection",
         {"ClearOverlayBoxes", "UniteOverlayBoxes", "OutlineOverlays"}},
        {&kOverlayKernels,
         "chrono_batch_render.overlay.kernels.picks",
         {"ClearOverlayPicks", "MeasureOverlayPicks", "ChooseOverlayPicks", "ResolveOverlayPicks"}},
        {&kRenderKernels,
         "chrono_batch_render.render.kernels.query.ray_cast",
         {"CastClosest", "CastAny"}},
        {&kRenderKernels,
         "chrono_batch_render.render.kernels.render.render",
         {"RenderMinimalData", "RenderMinimalDisplayColor", "RenderMinimalTextureDiffuse",
          "RenderMinimalFull", "RenderRealTime", "RenderRealTimeBands", "RenderLidar",
          "RenderScatterPoints", "RenderRadar"}},
        {&kSceneKernels,
         "chrono_batch_render.stage.columns.kernels.cells",
         {"CopyWords", "ScatterCells", "GatherCells", "CloneRows", "FillCells", "WritePoses",
          "ReadPoses", "GatherRows"}},
        {&kSceneKernels,
         "chrono_batch_render.scene.tables.kernels.bounds",
         {"PrimBounds", "SlotBounds", "InstancerBounds", "FinishBoxes"}},
        {&kSceneKernels,
         "chrono_batch_render.stage.columns.kernels.changes",
         {"StampRows", "CompactChanged", "CompactWorlds"}},
        {&kSceneKernels,
         "chrono_batch_render.scene.tables.kernels.meshes",
         {"SmoothNormals", "FinishShapeBounds", "FaceSizes", "FaceTriangles", "TopologySizes",
          "EmitTriangles", "WriteAdjacency", "WriteStarts"}},
        {&kSceneKernels,
         "chrono_batch_render.scene.tables.kernels.transforms",
         {"ResolveRows", "WriteMotions"}},
        {&kSceneKernels, "chrono_batch_render.scene.tables.kernels.instances", {"WriteInstances"}},
        {&kSceneKernels,
         "chrono_batch_render.scene.tables.kernels.lights",
         {"WriteDomeMaps", "WriteDomeRows", "WriteDomeMarginals", "WriteLights", "WriteLightCdf",
          "WriteCollections", "WriteLightShapes"}},
        {&kSceneKernels, "chrono_batch_render.scene.tables.kernels.materials", {"WriteMaterials"}},
        {&kSceneKernels,
         "chrono_batch_render.scene.tables.kernels.sensors",
         {"WriteCameras", "WriteLidars", "WriteRadars"}},
        {&kSceneKernels, "chrono_batch_render.scene.tables.kernels.surfaces", {"WriteSurfaces"}},
        {&kSceneKernels,
         "chrono_batch_render.scene.tables.kernels.tangents",
         {"GroupTangents", "CornerTangents"}},
        {&kTextureKernels, "chrono_batch_render.texture.kernels.mips", {"DownsampleLevels"}},
    };
}

bool EnvironmentFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) == "1";
}

template <class Traits, class Load>
void CompileEveryKernel(Backend backend, Load load, std::span<const char* const> targets,
                        std::span<const char* const> everyKernelTargets) {
    if (!EnvironmentFlag("CHRONO_BATCH_RENDER_TEST_FULL")) {
        GTEST_SKIP() << "compiles for every target only when CHRONO_BATCH_RENDER_TEST_FULL=1";
    }
    const compute::RuntimeCompiler* compiler = nullptr;
    try {
        compiler = &load();
    } catch (const UnsupportedError& error) {
        if (EnvironmentFlag("CHRONO_BATCH_RENDER_TEST_REQUIRE_COMPILERS")) {
            FAIL() << error.what();
        }
        GTEST_SKIP() << error.what();
    }
    const DiskCache cache(UserCacheDirectory());
    const SourceFiles sources =
        material::MaterialDispatchSources(material::MaterialDefinitionSet{});
    const compute::SourceHeader prelude{kPreludeHeader, Traits::Prelude()};
    bool first = true;
    for (const RuntimeProgram& program : RuntimePrograms()) {
        const std::vector<std::string> modules = {program.module};
        for (const char* entry : program.entries) {
            SCOPED_TRACE(program.module + "." + entry);
            const KernelCode code =
                Compile(backend, Traversal::Software, Sampling::Software, false, program.library,
                        sources, modules, entry, "runtime compilers", cache);
            for (const char* target : targets) {
                if (!first && std::ranges::find(everyKernelTargets, std::string_view(target)) ==
                                  everyKernelTargets.end()) {
                    continue;
                }
                SCOPED_TRACE(target);
                std::string object;
                EXPECT_NO_THROW(object = compiler
                                             ->CompileCached(code.code, {&prelude, 1},
                                                             Traits::CompileOptions(
                                                                 target, code.shape.groupSize,
                                                                 code.shape.maxRegisters, false),
                                                             entry, cache)
                                             .code);
                EXPECT_FALSE(object.empty());
            }
            first = false;
        }
    }
}

TEST(RuntimeCompilersTest, KernelsCompileForEveryNvidiaTarget) {
    const char* const targets[] = {"sm_75", "sm_86", "sm_90", "sm_120"};
    CompileEveryKernel<cuda::Traits>(Backend::Cuda, cuda::GetNvrtc, targets, targets);
}

TEST(RuntimeCompilersTest, KernelsCompileForEveryAmdTarget) {
    const char* const targets[] = {"gfx90a", "gfx942", "gfx950", "gfx1100"};
    const char* const everyKernel[] = {"gfx942", "gfx1100"};
    CompileEveryKernel<hip::Traits>(Backend::Hip, hip::GetHiprtc, targets, everyKernel);
}

}
}
