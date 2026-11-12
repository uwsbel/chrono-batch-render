// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "chrono_batch_render/app/viewer/panels/panels.h"
#include "chrono_batch_render/app/viewer/widgets/widgets.h"
#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render::app {
namespace {

constexpr size_t kRecentShown = 12;

std::string Labelled(const char* icon, const char* text) {
    return std::string(icon) + "  " + text;
}

void DrawSequence(Capture& capture) {
    CaptureSettings& settings = capture.settings;
    ImGui::SeparatorText("Sequence");
    const float field = 5.0f * ImGui::GetFontSize();
    ImGui::BeginDisabled(capture.Recording());
    ImGui::SetNextItemWidth(field);
    ImGui::InputInt("Frames", &settings.frames, 0);
    settings.frames = std::max(settings.frames, 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(field);
    ImGui::InputDouble("fps", &settings.fps, 0, 0, "%g");
    settings.fps = std::max(settings.fps, 1.0);
    ImGui::SetItemTooltip("The video's frame rate; recording takes each frame the viewport "
                          "renders");
    static const bool ffmpeg = !FindFfmpeg().empty();
    const bool encodable = ffmpeg && settings.format != ImageFormat::Exr;
    const char* videos[] = {"Frames only", "MP4 (H.264)", "ProRes 422 HQ (.mov)"};
    int video = encodable ? int(settings.video) : 0;
    ImGui::BeginDisabled(!encodable);
    ImGui::SetNextItemWidth(12.0f * ImGui::GetFontSize());
    if (ImGui::Combo("Video", &video, videos, int(std::size(videos)))) {
        settings.video = VideoFormat(video);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(video == 0);
    ImGui::Checkbox("Keep frames", &settings.keepFrames);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!ffmpeg) {
        ImGui::TextDisabled("ffmpeg isn't on the PATH, so sequences stay frames");
    } else if (settings.format == ImageFormat::Exr) {
        ImGui::TextDisabled("EXR sequences stay frames");
    }
    ImGui::EndDisabled();
    if (capture.Recording()) {
        if (ImGui::Button(Labelled(icons::kSquare, "Stop").c_str())) {
            capture.Stop();
        }
        ImGui::SameLine();
        const std::string label =
            std::to_string(capture.Recorded()) + " / " + std::to_string(settings.frames);
        ImGui::ProgressBar(float(capture.Recorded()) / float(settings.frames), {-1.0f, 0.0f},
                           label.c_str());
        return;
    }
    ImGui::BeginDisabled(capture.Encoding());
    if (ImGui::Button(Labelled(icons::kVideo, "Record").c_str())) {
        capture.Record();
    }
    ImGui::EndDisabled();
    if (capture.Encoding()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Encoding the last video...");
    }
}

void OpenFolder(const std::filesystem::path& folder) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
#if defined(_WIN32)
    const std::string command = "explorer \"" + folder.string() + "\"";
#elif defined(__APPLE__)
    const std::string command = "open \"" + folder.string() + "\" &";
#else
    const std::string command = "xdg-open \"" + folder.string() + "\" >/dev/null 2>&1 &";
#endif
    if (std::system(command.c_str()) != 0) {
        Log(LogLevel::Warning, "couldn't open {}", folder.string());
    }
}

void DrawOptions(CaptureSettings& settings) {
    ImGui::BeginDisabled(settings.format == ImageFormat::Exr);
    ImGui::Checkbox("Include overlays", &settings.overlays);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Composite the viewport's overlays, like the grid, axes, gizmos and XR "
                          "controllers, into PNG and JPEG captures");
    ImGui::BeginDisabled(settings.format != ImageFormat::Png);
    ImGui::Checkbox("Transparent background", &settings.transparent);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Keep the rendered alpha in PNGs; off, they're opaque");
    ImGui::BeginDisabled(settings.format != ImageFormat::Jpeg);
    ImGui::SetNextItemWidth(8.0f * ImGui::GetFontSize());
    ImGui::SliderInt("JPEG quality", &settings.jpegQuality, 1, 100);
    ImGui::EndDisabled();
}

void DrawCapture(Capture& capture, FileDialog& files) {
    CaptureSettings& settings = capture.settings;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fields = -ImGui::CalcTextSize("Name pattern").x - style.ItemInnerSpacing.x;
    const float button = ImGui::GetFrameHeight();
    std::string folder = settings.folder.string();
    ImGui::SetNextItemWidth(fields - 2 * (button + style.ItemInnerSpacing.x));
    if (ImGui::InputText("##folder", &folder)) {
        settings.folder = folder;
    }
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    if (IconButton("choose", icons::kFolder, "Choose the folder")) {
        files.Open({.title = "Capture Folder", .folder = true, .start = settings.folder},
                   [&settings](const std::filesystem::path& chosen) { settings.folder = chosen; });
    }
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    if (IconButton("open", icons::kFolderOpen, "Open the folder")) {
        OpenFolder(settings.folder);
    }
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted("Folder");
    ImGui::SetNextItemWidth(fields);
    ImGui::InputText("Name pattern", &settings.pattern);
    ImGui::SetItemTooltip("{scene}, {camera}, {date}, {time} and {frame} fill in; existing "
                          "files are never overwritten");
    const char* formats[] = {"PNG", "JPEG", "EXR"};
    int format = int(settings.format);
    ImGui::SetNextItemWidth(8.0f * ImGui::GetFontSize());
    if (ImGui::Combo("Format", &format, formats, int(std::size(formats)))) {
        settings.format = ImageFormat(format);
    }
    ImGui::SetItemTooltip("EXR keeps float outputs' values");
    DrawOptions(settings);
    if (ImGui::Button(Labelled(icons::kDownload, "Screenshot").c_str())) {
        capture.Screenshot();
    }
    ImGui::SetItemTooltip("Save the active viewport's image (F10)");
    DrawSequence(capture);
    if (!capture.Recent().empty()) {
        ImGui::SeparatorText("Recent");
        for (size_t at = 0; at < capture.Recent().size() && at < kRecentShown; ++at) {
            const std::string path = capture.Recent()[at].string();
            if (ImGui::Selectable(path.c_str())) {
                ImGui::SetClipboardText(path.c_str());
            }
            ImGui::SetItemTooltip("Copy the path");
        }
    }
}

}

Panel CreateCapturePanel(Capture& capture, FileDialog& files) {
    return {"Capture", Dock::Bottom, [&capture, &files] { DrawCapture(capture, files); }};
}

}
