// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "chrono_batch_render/app/viewer/panels/xr.h"
#include "chrono_batch_render/device/device.h"
#include "chrono_batch_render/overlay/overlay.h"
#include "chrono_batch_render/render/render_output.h"
#include "chrono_batch_render/render/renderer.h"
#include "chrono_batch_render/scene/rotation.h"
#include "chrono_batch_render/util/log.h"
#include "chrono_batch_render/util/profiler.h"
#include "chrono_batch_render/xr/recording.h"
#include "chrono_batch_render/xr/rig.h"
#include "chrono_batch_render/xr/session.h"
#include "chrono_batch_render/xr/ui.h"

namespace chrono::batch_render::app {
namespace {

constexpr const char* kViewport = "XR Mirror";
constexpr const char* kRigPath = "/__chrono_batch_render_xr";
constexpr const char* kSpectatorPath = "/__chrono_batch_render_xr_spectator";

struct Desk {
    std::mutex mutex;
    Double3 head{0, 0, 1.6};
    double yaw = 90;
    double pitch = 0;
    std::set<std::string> keys;
    bool leftDown = false;
    bool dragging = false;
    float pressX = 0;
    float pressY = 0;
    std::chrono::steady_clock::time_point pressed;
    int clickFrames = 0;
    bool looking = false;
    float lastX = 0;
    float lastY = 0;
    double lastTime = 0;
};

Double3 Looking(double yaw, double pitch) {
    const double y = yaw * std::numbers::pi / 180;
    const double p = pitch * std::numbers::pi / 180;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

void WalkDesk(Desk& desk, double time, xr::SimulatedInput& input) {
    const std::lock_guard lock(desk.mutex);
    const double dt = desk.lastTime > 0 ? time - desk.lastTime : 0;
    desk.lastTime = time;
    const Double3 ahead = Looking(desk.yaw, 0);
    const Double3 right{ahead.y, -ahead.x, 0};
    const auto held = [&](const char* key) { return desk.keys.contains(key) ? 1.0 : 0.0; };
    const double forward = held("W") - held("S");
    const double sideways = held("D") - held("A");
    desk.head.x += 1.5 * dt * (ahead.x * forward + right.x * sideways);
    desk.head.y += 1.5 * dt * (ahead.y * forward + right.y * sideways);
    const Quatd look = LookAlong(Looking(desk.yaw, desk.pitch));
    input.head = {desk.head, look, true};
    for (const xr::Side side : {xr::Side::Left, xr::Side::Right}) {
        xr::Controller& controller = input.controllers[uint32_t(side)];
        const double offset = side == xr::Side::Left ? -0.2 : 0.2;
        controller.active = true;
        controller.pose = {{desk.head.x, desk.head.y, desk.head.z}, look, true};
        const Double3 at = Rotate(look, {offset, -0.3, -0.3});
        controller.pose.position.x += at.x;
        controller.pose.position.y += at.y;
        controller.pose.position.z += at.z;
        controller.aim = controller.pose;
    }
    const bool holding =
        desk.leftDown && !desk.dragging &&
        std::chrono::steady_clock::now() - desk.pressed > std::chrono::milliseconds(150);
    const bool clicking = desk.clickFrames > 0;
    if (clicking) {
        --desk.clickFrames;
    }
    if (holding || clicking) {
        input.Press(xr::Side::Right, "trigger/value", 1);
    }
    const float stickX = float(held("RightArrow") - held("LeftArrow"));
    const float stickY = float(held("UpArrow") - held("DownArrow"));
    if (stickX != 0) {
        input.Press(xr::Side::Left, "thumbstick/x", stickX);
    }
    if (stickY != 0) {
        input.Press(xr::Side::Left, "thumbstick/y", stickY);
    }
    if (held("Q") != held("E")) {
        input.Press(xr::Side::Right, "thumbstick/x", float(held("E") - held("Q")));
    }
    if (held("Space") != 0) {
        input.Press(xr::Side::Right, "thumbstick/y", 1);
    }
    if (held("G") != 0) {
        input.Press(xr::Side::Right, "squeeze/value", 1);
    }
    if (held("H") != 0) {
        input.Press(xr::Side::Left, "squeeze/value", 1);
    }
}

void MarkSessionPrims(Scene& scene, const Prim& prim) {
    scene.MarkSession(prim);
    for (const Prim& child : prim.Children()) {
        MarkSessionPrims(scene, child);
    }
}

void CollectCameras(const Prim& prim, std::vector<std::string>& cameras) {
    if (prim.IsA(SchemaId::Camera)) {
        cameras.push_back(prim.Path());
    }
    for (const Prim& child : prim.Children()) {
        CollectCameras(child, cameras);
    }
}

enum class Placement : int { Origin, StartAtCamera, FollowCamera, FollowPrim };

enum class Quality : int { RealTime, Full, TextureDiffuse, DisplayColor };

enum class MirrorEyes : int { Both, Left, Right, Off, Spectator };

struct XrSettings {
    xr::Backend backend = xr::Backend::Simulated;
    xr::ReferenceSpace space = xr::ReferenceSpace::Stage;
    float resolutionScale = 0.5f;
    float nearPlane = 0.05f;
    float farPlane = 1000;
    bool depthLayer = true;
    xr::BlendMode blend = xr::BlendMode::Opaque;
    xr::Side dominantHand = xr::Side::Right;

    Placement placement = Placement::StartAtCamera;
    std::string camera;
    std::string prim;
    bool smoothFollow = false;
    float followLag = 0.3f;
    bool onFloor = true;
    float floor = 0;
    bool fixedEyeHeight = false;
    float eyeHeight = 1.6f;
    Quality quality = Quality::Full;
    float userScale = 1;
    xr::LocomotionDesc locomotion;
    float poseSmoothing = 0;
    Float3 poseOffsetCm{0, 0, 0};
    float poseTiltDegrees = 0;
    xr::RigVisuals visuals;
    MirrorEyes mirrorEyes = MirrorEyes::Both;
    std::string replay;
    std::string recordTo = "xr-input.cbrxr";
    bool recording = false;
    float spectatorDistance = 2.5f;
    float spectatorHeight = 0.8f;
    bool wristMenu = true;
    bool haptics = true;
    xr::GrabSnapping snapping;
    ImageFit mirrorFit = ImageFit::Contain;
};

template <class Enum>
bool EnumCombo(const char* label, Enum& value, std::initializer_list<const char*> names) {
    int index = int(value);
    const std::vector<const char*> items(names);
    if (ImGui::Combo(label, &index, items.data(), int(items.size()))) {
        value = Enum(index);
        return true;
    }
    return false;
}

void Tip(const char* text) {
    ImGui::SetItemTooltip("%s", text);
}

enum Apply : uint32_t { kAnchor = 1, kQuality = 2, kLive = 4 };

struct Mirror {
    device::Tensor color;
    device::Operation ready;
    MirrorEyes eyes = MirrorEyes::Both;
};

struct Requests {
    std::optional<XrSettings> settings;
    uint32_t apply = 0;
    std::optional<float> refreshRate;
    std::optional<std::vector<std::string>> selected;
};

struct Posts {
    std::optional<Mirror> mirror;
    std::vector<OverlayEvent> overlayEvents;
    std::optional<std::vector<std::string>> select;
    std::vector<xr::Grabbing::Moved> moves;
    std::optional<XrSettings> settings;
    std::optional<xr::SessionStatus> status;
    std::optional<std::string> error;
    bool finished = false;
};

class XrMode : public RenderDelegate, public XrPanel, public std::enable_shared_from_this<XrMode> {
  public:
    XrMode(Viewer& viewer, Ref<Scene> scene) : m_viewer(viewer), m_scene(std::move(scene)) {}

    ~XrMode() override {
        Join();
    }

    std::string_view Name() const override {
        return "XR";
    }

    std::optional<RenderFrame> Render(const RenderRequest&) override {
        return std::nullopt;
    }

    bool Running() const override {
        return m_session != nullptr;
    }

    uint64_t FramesPresented() const override {
        return m_presented.load(std::memory_order_relaxed);
    }

    void Start() override {
        if (Running()) {
            return;
        }
        m_error.clear();
        try {
            xr::SessionDesc desc;
            desc.application = "Chrono Batch Render viewer";
            desc.backend = m_settings.backend;
            desc.referenceSpace = m_settings.space;
            desc.resolutionScale = m_settings.resolutionScale;
            desc.nearPlane = m_settings.nearPlane;
            desc.farPlane = std::max(m_settings.farPlane, m_settings.nearPlane * 2);
            desc.depthLayer = m_settings.depthLayer;
            desc.blendMode = m_settings.blend;
            desc.dominantHand = m_settings.dominantHand;
            desc.simulated.realTime = true;
            const bool replaying = !m_settings.replay.empty();
            if (replaying) {
                desc.backend = xr::Backend::Simulated;
                desc.simulated.script = xr::Recording::Load(m_settings.replay).Script();
                desc.features = {xr::Feature::HandTracking, xr::Feature::BodyTracking,
                                 xr::Feature::FaceTracking, xr::Feature::EyeGaze};
            } else {
                const std::shared_ptr<Desk> desk = m_desk;
                desc.simulated.script = [desk](uint64_t, double time, xr::SimulatedInput& input) {
                    WalkDesk(*desk, time, input);
                };
            }
            const std::string viewport = m_viewer.ActiveViewport();
            m_viewportCamera = viewport.empty() ? "" : m_viewer.CameraPath(viewport);
            m_session = std::make_unique<xr::Session>(desc);
            m_rig.emplace(m_scene, *m_session, xr::RigDesc{.path = kRigPath});
            xr::PointerDesc pointers = m_rig->GetPointers();
            pointers.wait = false;
            m_rig->SetPointers(pointers);
            MarkSessionPrims(*m_scene, m_rig->Root());
            m_renderer = std::make_shared<Renderer>(
                m_scene, RendererDesc{.streaming = Streaming::Progressive});
            m_live = m_settings;
            m_shownSelection = m_viewer.Selected();
            m_selected = m_shownSelection;
            m_status = m_session->Status();
            if (const std::optional<std::string> error = ApplyAnchor()) {
                m_error = *error;
            }
            ApplyQuality();
            ApplyLive();
            m_presented = 0;
            m_fps = 0;
            m_viewer.AddViewport({.name = kViewport,
                                  .delegate = shared_from_this(),
                                  .hud = false,
                                  .toolbar = false,
                                  .replaces = viewport,
                                  .footer = [this] { DrawInfoBar(); },
                                  .fit = m_settings.mirrorFit});
            m_handlers.push_back(m_viewer.AddBeginFrameHandler([this] { Drain(); }));
            if (desc.backend == xr::Backend::Simulated && !replaying) {
                Watch();
            }
            m_recording.Clear();
            m_recorded = 0;
            m_stop = false;
            m_thread = std::thread([this] { Run(); });
        } catch (const std::exception& error) {
            Log(LogLevel::Error, "XR didn't start: {}", error.what());
            m_error = error.what();
            Stop();
        }
    }

    void Stop() override {
        Join();
        for (const uint64_t handler : m_handlers) {
            m_viewer.RemoveHandler(handler);
        }
        m_handlers.clear();
        const std::vector<std::string> viewports = m_viewer.Viewports();
        if (std::ranges::find(viewports, kViewport) != viewports.end()) {
            m_viewer.RemoveViewport(kViewport);
        }
        {
            const std::lock_guard lock(m_mutex);
            m_requests = {};
            m_posts = {};
        }
        m_stopRequested = false;
        m_renderer.reset();
        m_rig.reset();
        m_session.reset();
        if (const std::optional<Prim> rig = m_scene->Find(kRigPath)) {
            m_scene->Remove(*rig);
        }
        if (const std::optional<Prim> spectator = m_scene->Find(kSpectatorPath)) {
            m_scene->Remove(*spectator);
        }
    }

    void SaveRecording() {
        if (m_recording.Size() == 0) {
            return;
        }
        const std::filesystem::path path = m_live.recordTo;
        try {
            m_recording.Save(path);
            Log(LogLevel::Info, "saved {} frames of XR input to {}", m_recording.Size(),
                path.string());
        } catch (const std::exception& error) {
            Post([&](Posts& posts) { posts.error = error.what(); });
        }
        m_recording.Clear();
    }

    void Replay(std::filesystem::path recording) override {
        m_settings.replay = recording.string();
    }

    void Record(std::filesystem::path file) override {
        m_settings.recording = !file.empty();
        if (!file.empty()) {
            m_settings.recordTo = file.string();
        }
        if (Running()) {
            Request(0);
        }
    }

    void Spectate(bool spectating) override {
        m_settings.mirrorEyes = spectating ? MirrorEyes::Spectator : MirrorEyes::Both;
        if (Running()) {
            Request(0);
        }
    }

    void Simulate(bool simulated) override {
        m_settings.backend = simulated ? xr::Backend::Simulated : xr::Backend::OpenXR;
    }

    void Draw() {
        if (Running()) {
            DrawStatus();
        } else {
            if (ImGui::Button("Start XR")) {
                Start();
            }
            if (m_settings.backend == xr::Backend::Simulated) {
                ImGui::TextWrapped("In the XR viewport, WASD walks, dragging looks around, a "
                                   "click selects what the laser points at and holding still "
                                   "pulls the trigger, holding G grabs it "
                                   "and H as well scales and turns it, holding Space aims and "
                                   "letting go teleports, the arrow keys move and Q and E turn.");
            }
        }
        if (!m_error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.89f, 0.34f, 0.32f, 1});
            ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::PopStyleColor();
        }
        const std::pair<const char*, void (XrMode::*)()> sections[] = {
            {"session", &XrMode::DrawSession},
            {"placement", &XrMode::DrawPlacement},
            {"view", &XrMode::DrawView},
            {"movement", &XrMode::DrawMovement},
            {"controllers", &XrMode::DrawControllers},
            {"mirror", &XrMode::DrawMirror},
            {"recording", &XrMode::DrawRecording}};
        for (const auto& [id, draw] : sections) {
            ImGui::PushID(id);
            (this->*draw)();
            ImGui::PopID();
        }
    }

  private:
    void Run() {
        profiler::SetThreadName("XR");
        try {
            while (!m_stop.load(std::memory_order_acquire) && XrFrame()) {
            }
            SaveRecording();
        } catch (const std::exception& error) {
            Log(LogLevel::Error, "XR stopped: {}", error.what());
            const std::lock_guard lock(m_mutex);
            m_posts.error = error.what();
        }
        const std::lock_guard lock(m_mutex);
        m_posts.finished = true;
    }

    void Join() {
        m_stop.store(true, std::memory_order_release);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    template <class Change> void Post(Change change) {
        const std::lock_guard lock(m_mutex);
        change(m_posts);
    }

    void TakeRequests() {
        Requests requests;
        {
            const std::lock_guard lock(m_mutex);
            requests = std::exchange(m_requests, {});
        }
        if (requests.settings) {
            m_live = std::move(*requests.settings);
        }
        if (requests.selected) {
            m_selected = std::move(*requests.selected);
        }
        if (requests.apply & kAnchor) {
            const std::optional<std::string> error = ApplyAnchor();
            Post([&](Posts& posts) { posts.error = error.value_or(""); });
        }
        if (requests.apply & kQuality) {
            ApplyQuality();
        }
        if (requests.apply & kLive) {
            ApplyLive();
        }
        if (requests.refreshRate) {
            m_session->SetRefreshRate(*requests.refreshRate);
        }
    }

    bool XrFrame() {
        CHRONO_BATCH_RENDER_ZONE("XR/Frame");
        TakeRequests();
        if (!m_session->Running()) {
            return false;
        }
        xr::Frame frame = m_session->BeginFrame();
        if (m_live.recording && !m_live.recordTo.empty()) {
            m_recording.Add(frame);
            m_recorded.store(m_recording.Size(), std::memory_order_relaxed);
        } else {
            SaveRecording();
        }
        m_rig->Update(frame);
        m_controls.Apply(*m_rig, frame);
        const std::vector<xr::PointerEvent> events =
            m_pointing.Update(*m_rig, *m_renderer->GetOverlays(), frame);
        m_ui.Begin(*m_rig, frame, m_pointing, events);
        bool stopping = false;
        if (m_live.wristMenu) {
            stopping = DrawWristMenu(frame);
        }
        m_ui.End(*m_renderer->GetOverlays());
        if (m_live.haptics) {
            for (const xr::Side poking : m_ui.Pokes()) {
                frame.Haptics(poking, 0.4f, 0.015);
            }
        }
        for (const xr::PointerEvent& event : events) {
            Point(event);
        }
        if (m_live.haptics) {
            xr::Buzz(frame, events);
        }
        const bool holding = m_grabbing.Held().has_value();
        m_grabbing.SetSnapping(m_live.snapping);
        std::optional<xr::Grabbing::Moved> moved = m_grabbing.Update(*m_rig, frame, m_pointing);
        if (holding != m_grabbing.Held().has_value() && m_live.haptics) {
            frame.Haptics(frame.dominantHand, holding ? 0.3f : 0.6f, 0.03);
        }
        if (!holding && m_grabbing.Held()) {
            m_selected = {*m_grabbing.Held()};
            Post([&](Posts& posts) { posts.select = m_selected; });
        }
        if (moved) {
            Post([&](Posts& posts) { posts.moves.push_back(std::move(*moved)); });
        }
        xr::RigVisuals visuals = m_live.visuals;
        visuals.teleport = m_controls.Aiming();
        m_rig->Draw(*m_renderer->GetOverlays(), frame, visuals);
        m_pointing.Draw(*m_renderer->GetOverlays(), visuals);
        DrawSelection();
        const bool spectating = m_live.mirrorEyes == MirrorEyes::Spectator;
        Spectate(frame, spectating);
        if (frame.shouldRender) {
            m_session->LatchViews(frame);
            m_rig->UpdateViews(frame);
            m_scene->Advance(frame.deltaTime);
            std::vector<std::string> products = {m_rig->Product()};
            if (spectating) {
                products.push_back(std::string(kSpectatorPath) + "/Render/Product");
            }
            RenderOutput output = m_renderer->Step(products);
            m_session->EndFrame(frame, output);
            Count();
            PostMirror(output);
        } else {
            m_session->EndFrame(frame, RenderOutput());
        }
        PostStatus();
        return !stopping;
    }

    std::string AnchorCamera() const {
        return m_live.camera.empty() ? m_viewportCamera : m_live.camera;
    }

    xr::AnchorDesc Anchor() const {
        xr::AnchorDesc anchor;
        const XrSettings& s = m_live;
        switch (s.placement) {
        case Placement::Origin:
            break;
        case Placement::StartAtCamera:
            anchor.mode = xr::Anchor::Camera;
            anchor.target = AnchorCamera();
            if (s.onFloor) {
                anchor.floor = s.floor;
            }
            break;
        case Placement::FollowCamera:
        case Placement::FollowPrim:
            anchor.mode = s.smoothFollow ? xr::Anchor::FollowSmoothed : xr::Anchor::Follow;
            anchor.target = s.placement == Placement::FollowCamera ? AnchorCamera() : s.prim;
            anchor.smoothing = s.followLag;
            break;
        }
        if (anchor.mode != xr::Anchor::Origin && anchor.target.empty()) {
            anchor = {};
        }
        if (s.fixedEyeHeight) {
            anchor.fixedHeight = s.eyeHeight;
        }
        return anchor;
    }

    std::optional<std::string> ApplyAnchor() {
        try {
            m_rig->SetAnchor(Anchor());
            return std::nullopt;
        } catch (const std::exception& error) {
            m_rig->SetAnchor({});
            return error.what();
        }
    }

    void ApplyQuality() {
        const View product = m_scene->Select(m_rig->Product());
        if (m_live.quality == Quality::RealTime) {
            product.Get(RenderProduct::kRenderMode).Set(RenderMode::RealTime);
            return;
        }
        constexpr MinimalMode kModes[] = {MinimalMode::Full, MinimalMode::Full,
                                          MinimalMode::TextureDiffuse, MinimalMode::DisplayColor};
        product.Get(RenderProduct::kRenderMode).Set(RenderMode::Minimal);
        product.Get(RenderProduct::kMode).Set(kModes[int(m_live.quality)]);
    }

    void ApplyLive() {
        const XrSettings& s = m_live;
        m_rig->SetUserScale(std::max(double(s.userScale), 1e-3));
        m_rig->SetLocomotion(s.locomotion);
        xr::PoseFilter filter;
        filter.smoothing = s.poseSmoothing;
        filter.offset.position = {s.poseOffsetCm.x * 0.01, s.poseOffsetCm.y * 0.01,
                                  s.poseOffsetCm.z * 0.01};
        filter.offset.orientation = FromAxisAngle({1, 0, 0}, s.poseTiltDegrees);
        for (const xr::Side hand : {xr::Side::Left, xr::Side::Right}) {
            m_session->SetPoseFilter(hand, filter);
        }
    }

    bool DrawWristMenu(const xr::Frame& frame) {
        const xr::Side off =
            frame.dominantHand == xr::Side::Right ? xr::Side::Left : xr::Side::Right;
        if (!m_ui.BeginPanel({.name = "wrist", .mount = xr::PanelMount::Hand, .hand = off})) {
            m_ui.EndPanel();
            return false;
        }
        XrSettings& s = m_live;
        bool changed = false;
        bool stopping = false;
        m_ui.Text("Chrono Batch Render");
        if (m_ui.Button("Recenter")) {
            const std::string error = ApplyAnchor().value_or("");
            Post([&](Posts& posts) { posts.error = error; });
        }
        changed |= m_ui.Checkbox("Lasers", s.visuals.lasers);
        changed |= m_ui.Checkbox("Hands", s.visuals.hands);
        float speed = float(s.locomotion.speed);
        if (m_ui.Slider("Speed", speed, 0.5f, 6)) {
            s.locomotion.speed = speed;
            ApplyLive();
            changed = true;
        }
        bool mirror = s.mirrorEyes != MirrorEyes::Off;
        if (m_ui.Checkbox("Desktop mirror", mirror)) {
            s.mirrorEyes = mirror ? MirrorEyes::Both : MirrorEyes::Off;
            changed = true;
        }
        if (m_ui.Button("Stop XR")) {
            stopping = true;
        }
        m_ui.EndPanel();
        if (changed) {
            Post([&](Posts& posts) { posts.settings = s; });
        }
        return stopping;
    }

    void Point(const xr::PointerEvent& event) {
        if (event.target.overlay && event.target.overlay->overlay == m_ui.style.layer) {
            return;
        }
        if (event.target.overlay) {
            constexpr OverlayGesture kGestures[] = {OverlayGesture::Hover, OverlayGesture::Leave,
                                                    OverlayGesture::Press, OverlayGesture::Release,
                                                    OverlayGesture::Click};
            const auto narrow = [](Double3 v) {
                return Float3{float(v.x), float(v.y), float(v.z)};
            };
            OverlayEvent overlayEvent{.gesture = kGestures[uint32_t(event.gesture)],
                                      .viewport = kViewport,
                                      .hit = *event.target.overlay,
                                      .ray = {narrow(event.origin), narrow(event.direction)}};
            Post([&](Posts& posts) { posts.overlayEvents.push_back(std::move(overlayEvent)); });
            return;
        }
        if (event.gesture != xr::PointerGesture::Click) {
            return;
        }
        std::optional<Prim> prim;
        if (event.target.prim != kNoPrim) {
            prim = m_scene->ByIndex(event.target.prim);
        }
        if (prim && prim->IsSession()) {
            return;
        }
        m_selected = prim ? std::vector<std::string>{prim->Path()} : std::vector<std::string>{};
        Post([&](Posts& posts) { posts.select = m_selected; });
    }

    void DrawSelection() {
        CHRONO_BATCH_RENDER_ZONE("XR/Selection");
        Overlay overlay;
        std::vector<uint32_t> prims;
        for (const std::string& path : m_selected) {
            if (const std::optional<Prim> prim = m_scene->Find(path)) {
                prims.push_back(prim->Index());
            }
        }
        if (!prims.empty()) {
            OverlayStyle style;
            style.color = {1.0f, 0.62f, 0.16f, 1};
            style.pickable = false;
            overlay.Outline(prims, style);
        }
        m_renderer->GetOverlays()->Set(m_live.visuals.layer + " selection", std::move(overlay));
    }

    void Spectate(const xr::Frame& frame, bool spectating) {
        CHRONO_BATCH_RENDER_ZONE("XR/Spectator");
        Overlay marker;
        if (spectating && frame.head.valid) {
            const std::string root = kSpectatorPath;
            if (!m_scene->Find(root)) {
                m_scene->Define<Xform>(root);
                m_scene->Define<Camera>(root + "/Camera", {.focalLength = 18,
                                                           .horizontalAperture = 36,
                                                           .verticalAperture = 20.25f,
                                                           .clippingRange = {0.05f, 1000}});
                m_scene->Define<Scope>(root + "/Render");
                m_scene->Define<RenderVar>(root + "/Render/LdrColor", {.sourceName = "LdrColor"});
                m_scene->Define<RenderProduct>(root + "/Render/Product",
                                               {.camera = {root + "/Camera"},
                                                .resolution = {1280, 720},
                                                .orderedVars = {root + "/Render/LdrColor"},
                                                .overlays = {"*"}});
                MarkSessionPrims(*m_scene, *m_scene->Find(root));
                m_spectatorAt.reset();
            }
            const xr::Pose head = m_rig->WorldPose(frame.head);
            Double3 ahead = Rotate(head.orientation, {0, 0, -1});
            ahead.z = 0;
            const double reach = std::hypot(ahead.x, ahead.y);
            ahead = reach > 1e-6 ? Double3{ahead.x / reach, ahead.y / reach, 0} : Double3{0, 1, 0};
            const Double3 wanted{head.position.x - ahead.x * m_live.spectatorDistance,
                                 head.position.y - ahead.y * m_live.spectatorDistance,
                                 head.position.z + m_live.spectatorHeight};
            Double3 at = m_spectatorAt.value_or(wanted);
            const double share = 1 - std::exp(-frame.deltaTime / 0.25);
            at = {at.x + (wanted.x - at.x) * share, at.y + (wanted.y - at.y) * share,
                  at.z + (wanted.z - at.z) * share};
            m_spectatorAt = at;
            const Prim camera = *m_scene->Find(root + "/Camera");
            camera.Set(Xformable::kTranslate, at);
            camera.Set(Xformable::kOrient, LookAt(at, head.position));
            OverlayStyle style;
            style.color = m_live.visuals.color;
            style.pickable = false;
            const Float3 centre{float(head.position.x), float(head.position.y),
                                float(head.position.z)};
            marker.Box(centre, {0.09f, 0.11f, 0.07f}, style, head.orientation);
            const Double3 facing = Rotate(head.orientation, {0, 0, -1});
            marker.Arrow(centre,
                         {float(head.position.x + 0.25 * facing.x),
                          float(head.position.y + 0.25 * facing.y),
                          float(head.position.z + 0.25 * facing.z)},
                         style);
        }
        m_renderer->GetOverlays()->Set(m_live.visuals.layer + " head", std::move(marker));
    }

    void PostMirror(const RenderOutput& output) {
        CHRONO_BATCH_RENDER_ZONE("XR/Mirror");
        const MirrorEyes shown = m_live.mirrorEyes;
        if (shown == MirrorEyes::Off) {
            return;
        }
        const std::string product = shown == MirrorEyes::Spectator
                                        ? std::string(kSpectatorPath) + "/Render/Product"
                                        : m_rig->Product();
        const std::string var = shown == MirrorEyes::Spectator
                                    ? std::string(kSpectatorPath) + "/Render/LdrColor"
                                    : m_rig->ColorVar();
        if (!output.Contains(product)) {
            return;
        }
        const FrameOutput& eyes = output[product].frames.back();
        device::Operation ready = output.Done();
        device::Tensor color = eyes.RenderVar(var).tensor;
        if (!eyes.overlay.Empty()) {
            color = CompositeOverlay(color, eyes.overlay, ready, &ready);
        }
        device::Device& device = m_scene->GetDevice();
        const Span<const int64_t> shape = color.Shape();
        device::Tensor mirror =
            device.CreateTensor(color.Type(), {shape.begin(), shape.end()}, "xr mirror");
        ready = device.Copy(color, mirror, ready);
        Post([&](Posts& posts) { posts.mirror = Mirror{std::move(mirror), ready, shown}; });
    }

    void PostStatus() {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_statusPosted < std::chrono::milliseconds(250)) {
            return;
        }
        m_statusPosted = now;
        xr::SessionStatus status = m_session->Status();
        Post([&](Posts& posts) { posts.status = std::move(status); });
    }

    void Count() {
        const auto now = std::chrono::steady_clock::now();
        if (m_presented.fetch_add(1, std::memory_order_relaxed) > 0) {
            const double seconds = std::chrono::duration<double>(now - m_lastPresent).count();
            if (seconds > 0) {
                const double fps = m_fps.load(std::memory_order_relaxed);
                m_fps.store(fps > 0 ? 0.9 * fps + 0.1 / seconds : 1 / seconds,
                            std::memory_order_relaxed);
            }
        }
        m_lastPresent = now;
    }

    void Request(uint32_t apply) {
        const std::lock_guard lock(m_mutex);
        m_requests.settings = m_settings;
        m_requests.apply |= apply;
    }

    void Drain() {
        Posts posts;
        {
            const std::lock_guard lock(m_mutex);
            posts = std::exchange(m_posts, {});
        }
        if (posts.error) {
            m_error = *posts.error;
        }
        if (posts.status) {
            m_status = std::move(*posts.status);
        }
        if (posts.settings) {
            m_settings = std::move(*posts.settings);
        }
        for (const OverlayEvent& event : posts.overlayEvents) {
            m_viewer.SendOverlayEvent(event);
        }
        if (posts.select) {
            m_viewer.Select(*posts.select);
            m_shownSelection = *posts.select;
        }
        for (const xr::Grabbing::Moved& moved : posts.moves) {
            RecordMove(moved);
        }
        if (posts.mirror && posts.mirror->eyes != MirrorEyes::Off) {
            const Mirror& mirror = *posts.mirror;
            const device::Tensor shown =
                mirror.eyes == MirrorEyes::Both ? mirror.color
                : mirror.eyes == MirrorEyes::Spectator
                    ? mirror.color.Select(0)
                    : mirror.color.Select(mirror.eyes == MirrorEyes::Left ? 0 : 1);
            m_viewer.Show(shown, mirror.ready, kViewport);
        }
        if (posts.finished || m_stopRequested) {
            Stop();
            return;
        }
        if (m_viewer.Selected() != m_shownSelection) {
            m_shownSelection = m_viewer.Selected();
            const std::lock_guard lock(m_mutex);
            m_requests.selected = m_shownSelection;
        }
    }

    void RecordMove(const xr::Grabbing::Moved& moved) {
        const Ref<Scene> scene = m_scene;
        const auto place = [scene, path = moved.path](Double3 translate, Quatd orient,
                                                      Float3 scale) {
            if (const std::optional<Prim> prim = scene->Find(path)) {
                prim->Set(Xformable::kTranslate, translate);
                prim->Set(Xformable::kOrient, orient);
                prim->Set(Xformable::kScale, scale);
            }
        };
        const bool resized = moved.scale.x != moved.scaleBefore.x ||
                             moved.scale.y != moved.scaleBefore.y ||
                             moved.scale.z != moved.scaleBefore.z;
        m_viewer.RecordEdit(
            (resized ? "Transform " : "Move ") + moved.path.substr(moved.path.rfind('/') + 1),
            [place, moved] { place(moved.translateBefore, moved.orientBefore, moved.scaleBefore); },
            [place, moved] { place(moved.translate, moved.orient, moved.scale); });
    }

    void DrawInfoBar() {
        if (!m_session) {
            return;
        }
        const xr::SessionStatus& status = m_status;
        const xr::SessionStats stats = m_session->Stats();
        const bool focused = status.state == "focused";
        const bool shown = focused || status.state == "visible";
        const ImVec4 lamp = focused ? ImVec4{0.30f, 0.75f, 0.35f, 1}
                            : shown ? ImVec4{0.91f, 0.70f, 0.24f, 1}
                                    : ImVec4{0.55f, 0.55f, 0.55f, 1};
        const float line = ImGui::GetTextLineHeight();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled({at.x + 0.5f * line, at.y + 0.5f * line},
                                                    0.32f * line, ImGui::GetColorU32(lamp));
        ImGui::Dummy({line, line});
        const auto field = [](const std::string& text) {
            ImGui::SameLine(0, 1.5f * ImGui::GetFontSize());
            ImGui::TextUnformatted(text.c_str());
        };
        ImGui::SameLine();
        ImGui::TextUnformatted(fmt::format("{} · {}",
                                           status.system.empty() ? status.runtime : status.system,
                                           status.state)
                                   .c_str());
        field(fmt::format("{} × {} per eye", status.resolution.x, status.resolution.y));
        if (status.refreshRate > 0) {
            field(fmt::format("{:.0f} Hz", status.refreshRate));
        }
        field(fmt::format("{:.0f} FPS", m_fps.load(std::memory_order_relaxed)));
        field(fmt::format("GPU {:.1f} ms", stats.gpu.mean * 1e3));
        if (stats.motionToPhoton.mean > 0) {
            const double app = stats.poseToSubmit.mean + stats.gpu.mean;
            field(fmt::format("Motion to photon {:.1f} ms (app {:.1f}, runtime {:.1f})",
                              stats.motionToPhoton.mean * 1e3, app * 1e3,
                              std::max(stats.motionToPhoton.mean - app, 0.0) * 1e3));
        }
        field(fmt::format("{} late · {} missed", stats.late, stats.missed));
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttons = ImGui::CalcTextSize("Recenter").x + ImGui::CalcTextSize("Stop XR").x +
                              4 * style.FramePadding.x + style.ItemSpacing.x;
        const float right =
            ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - style.ItemSpacing.x - buttons;
        ImGui::SameLine();
        if (ImGui::GetCursorScreenPos().x < right) {
            ImGui::SetCursorScreenPos(
                {right, ImGui::GetCursorScreenPos().y - style.FramePadding.y});
        }
        if (ImGui::SmallButton("Recenter")) {
            Request(kAnchor);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop XR")) {
            m_stopRequested = true;
        }
    }

    void Watch() {
        const std::shared_ptr<Desk> desk = m_desk;
        m_handlers.push_back(m_viewer.AddKeyHandler([desk](const KeyEvent& event) {
            const std::lock_guard lock(desk->mutex);
            if (event.action == InputAction::Press) {
                desk->keys.insert(event.key);
            } else if (event.action == InputAction::Release) {
                desk->keys.erase(event.key);
            }
            return false;
        }));
        m_handlers.push_back(m_viewer.AddMouseHandler([desk](const MouseEvent& event) {
            const std::lock_guard lock(desk->mutex);
            const bool ours = event.inViewport && event.viewport == kViewport;
            const auto now = std::chrono::steady_clock::now();
            if (event.action == InputAction::Press && ours && event.button == 0) {
                desk->leftDown = true;
                desk->dragging = false;
                desk->pressX = event.x;
                desk->pressY = event.y;
                desk->pressed = now;
            } else if (event.action == InputAction::Press && ours && event.button == 1) {
                desk->looking = true;
            } else if (event.action == InputAction::Release && event.button == 0) {
                const bool quick = now - desk->pressed <= std::chrono::milliseconds(150);
                if (desk->leftDown && !desk->dragging && quick) {
                    desk->clickFrames = 2;
                }
                desk->leftDown = false;
                desk->dragging = false;
            } else if (event.action == InputAction::Release && event.button == 1) {
                desk->looking = false;
            } else if (event.action == InputAction::Move) {
                const bool held = desk->leftDown && !desk->dragging &&
                                  now - desk->pressed > std::chrono::milliseconds(150);
                if (desk->leftDown && !desk->dragging && !held &&
                    std::hypot(event.x - desk->pressX, event.y - desk->pressY) > 4) {
                    desk->dragging = true;
                }
                if (desk->looking || desk->dragging) {
                    desk->yaw -= 0.2 * (event.x - desk->lastX);
                    desk->pitch =
                        std::clamp(desk->pitch - 0.2 * (event.y - desk->lastY), -85.0, 85.0);
                }
            }
            desk->lastX = event.x;
            desk->lastY = event.y;
            return ours && event.button >= 0;
        }));
    }

    void DrawStatus() {
        const xr::SessionStatus& status = m_status;
        const xr::SessionStats stats = m_session->Stats();
        ImGui::Text("%s", status.runtime.c_str());
        ImGui::Text("%s, %s", status.system.c_str(), status.state.c_str());
        ImGui::Text("%u x %u per eye at %.0f Hz", status.resolution.x, status.resolution.y,
                    status.refreshRate);
        ImGui::Text("%llu frames, %llu presented, %llu late, %llu missed",
                    static_cast<unsigned long long>(stats.frames),
                    static_cast<unsigned long long>(stats.presented),
                    static_cast<unsigned long long>(stats.late),
                    static_cast<unsigned long long>(stats.missed));
        ImGui::Text("CPU %.2f ms, p95 %.2f", stats.cpu.mean * 1e3, stats.cpu.p95 * 1e3);
        ImGui::Text("GPU %.2f ms, p95 %.2f", stats.gpu.mean * 1e3, stats.gpu.p95 * 1e3);
        ImGui::Text("Motion to photon %.2f ms, app %.2f", stats.motionToPhoton.mean * 1e3,
                    (stats.poseToSubmit.mean + stats.gpu.mean) * 1e3);
        if (!status.refreshRates.empty()) {
            ImGui::TextUnformatted("Refresh");
            for (const float rate : status.refreshRates) {
                ImGui::SameLine();
                if (ImGui::SmallButton(fmt::format("{:.0f} Hz", rate).c_str())) {
                    const std::lock_guard lock(m_mutex);
                    m_requests.refreshRate = rate;
                }
            }
        }
        if (ImGui::Button("Recenter")) {
            Request(kAnchor);
        }
        Tip("Put the tracked space back on its anchor, facing the way you look");
        ImGui::SameLine();
        if (ImGui::Button("Stop XR")) {
            m_stopRequested = true;
        }
    }

    void DrawSession() {
        if (!ImGui::CollapsingHeader("Session", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        XrSettings& s = m_settings;
        ImGui::BeginDisabled(Running());
        if (Running()) {
            ImGui::TextDisabled("Stop XR to change these");
        }
        EnumCombo("Backend", s.backend, {"OpenXR", "Simulated"});
        EnumCombo("Play space", s.space, {"Standing (stage)", "Seated (local)"});
        Tip("Standing puts the floor where the headset's guardian has it; seated starts the "
            "space at your head");
        ImGui::SliderFloat("Resolution", &s.resolutionScale, 0.1f, 2.0f, "%.2f×");
        Tip("A multiplier on the headset's recommended resolution per eye");
        ImGui::DragFloatRange2("Clipping", &s.nearPlane, &s.farPlane, 0.01f, 0.01f, 100000,
                               "near %.2f m", "far %.0f m", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Depth layer", &s.depthLayer);
        Tip("Send depth to the runtime so its reprojection can use it");
        EnumCombo("Blend", s.blend, {"Opaque", "Additive", "Passthrough"});
        EnumCombo("Dominant hand", s.dominantHand, {"Left", "Right"});
        Tip("The dominant hand teleports and turns, the other moves");
        ImGui::EndDisabled();
    }

    void DrawPlacement() {
        if (!ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        XrSettings& s = m_settings;
        bool changed =
            EnumCombo("Place", s.placement,
                      {"Scene origin", "Start at camera", "Follow camera", "Follow prim"});
        const bool cameraMode =
            s.placement == Placement::StartAtCamera || s.placement == Placement::FollowCamera;
        if (cameraMode) {
            changed |= DrawCameraChoice();
        }
        if (s.placement == Placement::FollowPrim) {
            changed |= ImGui::InputText("Prim", &s.prim, ImGuiInputTextFlags_EnterReturnsTrue);
            Tip("The path of the prim to ride along with, like a vehicle");
        }
        if (s.placement == Placement::FollowCamera || s.placement == Placement::FollowPrim) {
            changed |= ImGui::Checkbox("Smooth following", &s.smoothFollow);
            if (s.smoothFollow) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(6 * ImGui::GetFontSize());
                changed |= ImGui::SliderFloat("##lag", &s.followLag, 0.05f, 2, "%.2f s");
            }
        }
        if (s.placement == Placement::StartAtCamera) {
            changed |= ImGui::Checkbox("Start on the floor", &s.onFloor);
            Tip("Keep the camera's place and heading, but stand on the floor rather than float "
                "at the camera's height");
            if (s.onFloor) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(6 * ImGui::GetFontSize());
                changed |= ImGui::DragFloat("##floor", &s.floor, 0.05f, -1e4f, 1e4f, "%.2f m");
            }
        }
        changed |= ImGui::Checkbox("Fixed eye height", &s.fixedEyeHeight);
        Tip("Hold your eyes at this height whatever your real height, for seated play");
        if (s.fixedEyeHeight) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(6 * ImGui::GetFontSize());
            changed |= ImGui::DragFloat("##eyes", &s.eyeHeight, 0.01f, 0, 1e4f, "%.2f m");
        }
        if (changed && Running()) {
            Request(kAnchor);
        }
    }

    bool DrawCameraChoice() {
        XrSettings& s = m_settings;
        if (m_scene->StructureVersion() != m_structure) {
            m_structure = m_scene->StructureVersion();
            m_cameras.clear();
            const View roots = m_scene->Select("/*");
            for (const uint32_t root : roots.Prims()) {
                const Prim prim = m_scene->ByIndex(root);
                if (!prim.IsSession()) {
                    CollectCameras(prim, m_cameras);
                }
            }
        }
        const std::string shown = s.camera.empty() ? "Viewport camera" : s.camera;
        bool changed = false;
        if (ImGui::BeginCombo("Camera", shown.c_str())) {
            if (ImGui::Selectable("Viewport camera", s.camera.empty())) {
                s.camera.clear();
                changed = true;
            }
            ImGui::SetItemTooltip("The camera of the viewport you used last");
            for (const std::string& camera : m_cameras) {
                if (ImGui::Selectable(camera.c_str(), camera == s.camera)) {
                    s.camera = camera;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void DrawView() {
        if (!ImGui::CollapsingHeader("Quality and scale", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        XrSettings& s = m_settings;
        if (EnumCombo("Quality", s.quality,
                      {"Real-Time", "Full", "Texture Diffuse", "Display Color"})) {
            Request(kQuality);
        }
        Tip("Real-Time path traces lighting, which costs far more per frame than the Minimal "
            "modes");
        bool changed = ImGui::SliderFloat("Your scale", &s.userScale, 0.01f, 100, "%.3g×",
                                          ImGuiSliderFlags_Logarithmic);
        Tip("Above 1 you're a giant and the scene looks small; below 1 it looks huge");
        constexpr std::pair<const char*, float> kScales[] = {
            {"1:1", 1.0f}, {"Tabletop 1:20", 20.0f}, {"Ant 1:0.05", 0.05f}};
        for (const auto& [label, scale] : kScales) {
            if (ImGui::SmallButton(label)) {
                s.userScale = scale;
                changed = true;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (changed) {
            Request(kLive);
        }
    }

    void DrawMovement() {
        if (!ImGui::CollapsingHeader("Movement")) {
            return;
        }
        xr::LocomotionDesc& l = m_settings.locomotion;
        bool changed = EnumCombo("Move", l.moveMode, {"Smooth", "Grounded", "Fly"});
        Tip("Smooth glides flat, Grounded follows the ground, Fly rises and sinks with the "
            "dominant stick");
        float speed = float(l.speed);
        if (ImGui::SliderFloat("Speed", &speed, 0.1f, 20, "%.1f m/s",
                               ImGuiSliderFlags_Logarithmic)) {
            l.speed = speed;
            changed = true;
        }
        if (l.moveMode == xr::MoveMode::Fly) {
            float fly = float(l.flySpeed);
            if (ImGui::SliderFloat("Fly speed", &fly, 0.1f, 50, "%.1f m/s",
                                   ImGuiSliderFlags_Logarithmic)) {
                l.flySpeed = fly;
                changed = true;
            }
        }
        float snap = float(l.snapDegrees);
        if (ImGui::SliderFloat("Snap turn", &snap, 5, 90, "%.0f°")) {
            l.snapDegrees = snap;
            changed = true;
        }
        changed |= ImGui::Checkbox("Align to the floor", &l.floorAligned);
        Tip("Keep the play space level after teleports onto slopes");
        ImGui::SeparatorText("Grab snapping");
        xr::GrabSnapping& grabSnap = m_settings.snapping;
        float grid = float(grabSnap.grid);
        if (ImGui::SliderFloat("Grid", &grid, 0, 1, grid > 0 ? "%.2f m" : "off")) {
            grabSnap.grid = grid;
            changed = true;
        }
        Tip("Rounds the carried prim's position to a grid");
        float turn = float(grabSnap.turnDegrees);
        if (ImGui::SliderFloat("Turn steps", &turn, 0, 90, turn > 0 ? "%.0f°" : "off")) {
            grabSnap.turnDegrees = turn;
            changed = true;
        }
        Tip("Rounds the carried prim's heading");
        float surface = float(grabSnap.surface);
        if (ImGui::SliderFloat("Set down", &surface, 0, 5, surface > 0 ? "within %.1f m" : "off")) {
            grabSnap.surface = surface;
            changed = true;
        }
        Tip("Rests the carried prim on whatever is below it");
        if (changed) {
            Request(kLive);
        }
    }

    void DrawControllers() {
        if (!ImGui::CollapsingHeader("Controllers")) {
            return;
        }
        XrSettings& s = m_settings;
        xr::RigVisuals& v = s.visuals;
        bool shown = ImGui::Checkbox("Controllers##models", &v.controllers);
        ImGui::SameLine();
        shown |= ImGui::Checkbox("Hands", &v.hands);
        shown |= ImGui::Checkbox("Wrist menu", &s.wristMenu);
        Tip("A menu over your other hand, used with the laser");
        ImGui::SameLine();
        shown |= ImGui::Checkbox("Haptics", &s.haptics);
        Tip("Pulse the controller when the laser finds something, presses it or grabs it");
        shown |= ImGui::Checkbox("Lasers", &v.lasers);
        ImGui::SameLine();
        shown |= ImGui::Checkbox("Play area", &v.playArea);
        if (shown) {
            Request(0);
        }
        bool changed = ImGui::SliderFloat("Smoothing", &s.poseSmoothing, 0, 0.2f, "%.3f s");
        Tip("Steadies shaky controller poses at the cost of a little lag");
        changed |= ImGui::DragFloat3("Offset", &s.poseOffsetCm.x, 0.1f, -30, 30, "%.1f cm");
        changed |= ImGui::SliderFloat("Tilt", &s.poseTiltDegrees, -60, 60, "%.0f°");
        Tip("Pitches the controller poses, for lasers that point where your hand feels it should");
        if (changed) {
            Request(kLive);
        }
    }

    void DrawRecording() {
        if (!ImGui::CollapsingHeader("Recording")) {
            return;
        }
        XrSettings& s = m_settings;
        if (ImGui::InputText("Save to", &s.recordTo) && Running()) {
            Request(0);
        }
        if (Running()) {
            if (ImGui::Button(s.recording ? "Stop recording" : "Record")) {
                s.recording = !s.recording;
                Request(0);
            }
            if (s.recording) {
                ImGui::SameLine();
                ImGui::Text("%llu frames", static_cast<unsigned long long>(
                                               m_recorded.load(std::memory_order_relaxed)));
            }
        } else {
            ImGui::Checkbox("Record from the start", &s.recording);
        }
        Tip("Saves the headset's input when recording stops or XR does, to play back later");
        ImGui::BeginDisabled(Running());
        ImGui::InputTextWithHint("Replay", "a recording to play back instead", &s.replay);
        ImGui::EndDisabled();
        Tip("Starts XR on the simulated headset playing this recording back");
    }

    void DrawMirror() {
        if (!ImGui::CollapsingHeader("Mirror")) {
            return;
        }
        XrSettings& s = m_settings;
        if (EnumCombo("Show", s.mirrorEyes,
                      {"Both eyes", "Left eye", "Right eye", "Off", "Spectator"})) {
            Request(0);
        }
        Tip("Off stops drawing the headset's view on the desktop, which saves the GPU the work; "
            "the info bar stays. Spectator follows behind you with a third-person camera, which "
            "costs a second render.");
        if (s.mirrorEyes == MirrorEyes::Spectator) {
            bool moved = ImGui::SliderFloat("Behind", &s.spectatorDistance, 0.5f, 10, "%.1f m");
            moved |= ImGui::SliderFloat("Above", &s.spectatorHeight, -1, 5, "%.1f m");
            if (moved) {
                Request(0);
            }
        }
        if (EnumCombo("Fit", s.mirrorFit, {"Letterbox", "Crop", "Stretch"}) && Running()) {
            m_viewer.SetImageFit(s.mirrorFit, kViewport);
        }
    }

    Viewer& m_viewer;
    Ref<Scene> m_scene;
    std::optional<Double3> m_spectatorAt;
    xr::Recording m_recording;
    std::atomic<uint64_t> m_recorded = 0;
    std::shared_ptr<Desk> m_desk = std::make_shared<Desk>();

    XrSettings m_settings;
    std::string m_viewportCamera;
    uint64_t m_structure = ~uint64_t(0);
    std::vector<std::string> m_cameras;
    std::vector<uint64_t> m_handlers;
    std::string m_error;
    xr::SessionStatus m_status;
    std::vector<std::string> m_shownSelection;
    bool m_stopRequested = false;

    std::unique_ptr<xr::Session> m_session;
    std::optional<xr::Rig> m_rig;
    std::shared_ptr<Renderer> m_renderer;

    XrSettings m_live;
    std::vector<std::string> m_selected;
    xr::StandardControls m_controls;
    xr::Pointing m_pointing;
    xr::SpatialUi m_ui;
    xr::Grabbing m_grabbing;
    std::chrono::steady_clock::time_point m_lastPresent;
    std::chrono::steady_clock::time_point m_statusPosted;

    std::mutex m_mutex;
    Requests m_requests;
    Posts m_posts;
    std::atomic<bool> m_stop = false;
    std::atomic<uint64_t> m_presented = 0;
    std::atomic<double> m_fps = 0;
    std::thread m_thread;
};

}

std::shared_ptr<XrPanel> AddXrPanel(Viewer& viewer, Ref<Scene> scene) {
    const auto mode = std::make_shared<XrMode>(viewer, std::move(scene));
    viewer.AddPanel("XR", Dock::Right, [mode] { mode->Draw(); }, false);
    return mode;
}

}

extern "C" CHRONO_BATCH_RENDER_VIEWER_XR_API void
ChronoBatchRenderAddXrPanel(chrono::batch_render::app::Viewer* viewer,
                            const chrono::batch_render::Ref<chrono::batch_render::Scene>* scene,
                            std::shared_ptr<chrono::batch_render::app::XrPanel>* panel) {
    try {
        *panel = chrono::batch_render::app::AddXrPanel(*viewer, *scene);
    } catch (const std::exception& failure) {
        chrono::batch_render::Log(chrono::batch_render::LogLevel::Warning,
                                  "the viewer couldn't add its XR panel: {}", failure.what());
    }
}
