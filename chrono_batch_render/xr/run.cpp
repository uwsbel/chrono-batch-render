// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/xr/run.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>

#include <fmt/format.h>

#include "chrono_batch_render/chrono_batch_render.h"
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/recording.h"
#include "chrono_batch_render/xr/rig.h"
#include "chrono_batch_render/xr/session.h"
#include "chrono_batch_render/xr_export.h"

namespace chrono::batch_render::xr {
namespace {

std::atomic<int> g_interrupts = 0;

extern "C" void Interrupt(int) {
    if (++g_interrupts > 1) {
        std::signal(SIGINT, SIG_DFL);
    }
}

int Run(const RunDesc& run) {
    SessionDesc desc;
    desc.application = "Chrono Batch Render";
    desc.backend = run.simulated ? Backend::Simulated : Backend::OpenXR;
    desc.simulated.realTime = true;
    if (!run.replay.empty()) {
        desc.simulated.script = Recording::Load(run.replay).Script();
        desc.features = {Feature::HandTracking, Feature::BodyTracking, Feature::FaceTracking,
                         Feature::EyeGaze};
    }
    Recording recording;
    Session session(desc);
    const Ref<device::Device> device = desc.backend == Backend::OpenXR
                                           ? session.CreateDevice(run.device)
                                           : device::Device::Create(run.device);
    const Ref<Scene> scene = run.scene(*device);
    Rig rig(scene, session);
    rig.SetAnchor({.mode = Anchor::Camera, .target = run.camera, .floor = 0.0});
    Renderer renderer(scene, RendererDesc{.streaming = Streaming::Progressive});
    StandardControls controls;

    const SessionStatus status = session.Status();
    const std::string rate =
        status.refreshRate > 0 ? fmt::format(" at {:.0f} Hz", status.refreshRate) : "";
    std::printf("XR on %s through %s, %u x %u per eye%s. Ctrl+C stops.\n", status.system.c_str(),
                status.runtime.c_str(), status.resolution.x, status.resolution.y, rate.c_str());
    std::fflush(stdout);
    profiler::SetEnabled(run.profile);
    g_interrupts = 0;
    const auto previous = std::signal(SIGINT, Interrupt);
    bool exiting = false;
    uint64_t presented = 0;
    try {
        while (session.Running()) {
            if (!exiting && (g_interrupts > 0 || (run.frames > 0 && presented >= run.frames))) {
                session.RequestExit();
                exiting = true;
            }
            profiler::BeginFrame();
            CHRONO_BATCH_RENDER_ZONE("XR/Frame");
            Frame frame = session.BeginFrame();
            if (!run.record.empty()) {
                recording.Add(frame);
            }
            rig.Update(frame);
            controls.Apply(rig, frame);
            rig.Draw(*renderer.GetOverlays(), frame, {.teleport = controls.Aiming()});
            if (!frame.shouldRender) {
                session.EndFrame(frame, RenderOutput());
                continue;
            }
            session.LatchViews(frame);
            rig.UpdateViews(frame);
            scene->Advance(frame.deltaTime);
            session.EndFrame(frame, renderer.Step(rig.Product()));
            ++presented;
        }
    } catch (...) {
        std::signal(SIGINT, previous);
        throw;
    }
    std::signal(SIGINT, previous);
    if (!run.record.empty()) {
        recording.Save(run.record);
        std::printf("Recorded %zu frames, %.1f s, to %s.\n", recording.Size(), recording.Duration(),
                    run.record.string().c_str());
    }
    const SessionStats stats = session.Stats();
    std::printf("Presented %llu frames, %llu late and %llu missed. GPU %.2f ms, motion to photon "
                "%.1f ms on average.\n",
                static_cast<unsigned long long>(stats.presented),
                static_cast<unsigned long long>(stats.late),
                static_cast<unsigned long long>(stats.missed), stats.gpu.mean * 1e3,
                stats.motionToPhoton.mean * 1e3);
    if (run.profile) {
        const profiler::Report report = profiler::Summarize(profiler::Frames());
        std::fputs(profiler::ReportText(report).c_str(), stderr);
    }
    return 0;
}

}
}

extern "C" CHRONO_BATCH_RENDER_XR_API int
ChronoBatchRenderRunXr(const chrono::batch_render::xr::RunDesc* desc, std::string* error) {
    try {
        return chrono::batch_render::xr::Run(*desc);
    } catch (const std::exception& failure) {
        *error = failure.what();
        return 1;
    }
}
