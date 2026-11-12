// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/widgets/file_dialog.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

#include <fmt/chrono.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include "chrono_batch_render/app/viewer/theme/theme.h"
#include "chrono_batch_render/app/viewer/widgets/widgets.h"
#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render::app {

namespace fs = std::filesystem;

namespace {

constexpr size_t kRecentFolders = 8;

bool IsDirectory(const fs::path& path) {
    std::error_code error;
    return !path.empty() && fs::is_directory(path, error);
}

fs::path HomeDirectory() {
    for (const char* name : {"HOME", "USERPROFILE"}) {
        if (const char* home = std::getenv(name); home != nullptr && *home != '\0') {
            return home;
        }
    }
    return {};
}

std::string TimeText(fs::file_time_type time) {
    const auto system = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            time - fs::file_time_type::clock::now()));
    return fmt::format("{:%Y-%m-%d %H:%M}", system);
}

std::string Labelled(const char* icon, const std::string& text) {
    return std::string(icon) + "  " + text;
}

std::string FolderName(const fs::path& folder) {
    return folder.filename().empty() ? folder.string() : folder.filename().string();
}

}

fs::path DefaultPlacesFile() {
    fs::path config;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        config = xdg;
    } else if (const char* appData = std::getenv("APPDATA");
               appData != nullptr && *appData != '\0') {
        config = appData;
    } else if (const fs::path home = HomeDirectory(); !home.empty()) {
        config = home / ".config";
    } else {
        return {};
    }
    return config / "chrono_batch_render" / "places.json";
}

void FileDialog::SetPlacesFile(fs::path file) {
    m_placesFile = std::move(file);
    LoadPlaces();
}

void FileDialog::LoadPlaces() {
    if (m_placesFile.empty()) {
        return;
    }
    std::ifstream in(m_placesFile);
    if (!in) {
        return;
    }
    try {
        const nlohmann::json places = nlohmann::json::parse(in);
        const auto read = [&](const char* key, std::vector<fs::path>& folders) {
            if (const auto found = places.find(key); found != places.end()) {
                folders.clear();
                for (const nlohmann::json& folder : *found) {
                    folders.emplace_back(folder.get<std::string>());
                }
            }
        };
        read("favorites", m_favorites);
        read("recent", m_recent);
        if (m_recent.size() > kRecentFolders) {
            m_recent.resize(kRecentFolders);
        }
    } catch (const std::exception& error) {
        Log(LogLevel::Warning, "Reading the file dialog's folders from {}: {}",
            m_placesFile.string(), error.what());
    }
}

void FileDialog::SavePlaces() const {
    if (m_placesFile.empty()) {
        return;
    }
    const auto strings = [](const std::vector<fs::path>& folders) {
        nlohmann::json list = nlohmann::json::array();
        for (const fs::path& folder : folders) {
            list.push_back(folder.string());
        }
        return list;
    };
    std::error_code error;
    fs::create_directories(m_placesFile.parent_path(), error);
    std::ofstream out(m_placesFile);
    if (!out) {
        Log(LogLevel::Warning, "Can't write the file dialog's folders to {}",
            m_placesFile.string());
        return;
    }
    out << nlohmann::json{{"favorites", strings(m_favorites)}, {"recent", strings(m_recent)}}.dump(
               2)
        << '\n';
}

bool FileDialog::ToggleFavorite(const fs::path& folder) {
    const bool favorite = std::ranges::find(m_favorites, folder) == m_favorites.end();
    if (favorite) {
        m_favorites.push_back(folder);
    } else {
        std::erase(m_favorites, folder);
    }
    SavePlaces();
    return favorite;
}

void FileDialog::Forget(const fs::path& folder) {
    std::erase(m_recent, folder);
    SavePlaces();
}

bool FileFilter::Matches(const fs::path& path) const {
    const std::string extension = Lowercase(path.extension().string());
    return extensions.empty() || std::ranges::any_of(extensions, [&](const std::string& wanted) {
               return Lowercase(wanted) == extension;
           });
}

std::vector<DirectoryEntry> ListDirectory(const fs::path& directory, const FileFilter* filter,
                                          bool showHidden) {
    std::vector<DirectoryEntry> entries;
    for (const fs::directory_entry& item : fs::directory_iterator(directory)) {
        std::error_code error;
        DirectoryEntry entry{.name = item.path().filename().string()};
        if (!showHidden && entry.name.starts_with('.')) {
            continue;
        }
        entry.directory = item.is_directory(error);
        if (error) {
            continue;
        }
        if (!entry.directory) {
            if (filter != nullptr && !filter->Matches(item.path())) {
                continue;
            }
            entry.size = item.file_size(error);
            if (error) {
                entry.size = 0;
            }
        }
        entry.modified = item.last_write_time(error);
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries, [](const DirectoryEntry& a, const DirectoryEntry& b) {
        if (a.directory != b.directory) {
            return a.directory;
        }
        const std::string left = Lowercase(a.name);
        const std::string right = Lowercase(b.name);
        return left != right ? left < right : a.name < b.name;
    });
    return entries;
}

void FileDialog::Open(FileDialogOptions options, Done done) {
    m_options = std::move(options);
    m_done = std::move(done);
    m_open = true;
    m_popupRequested = true;
    m_filter = 0;
    m_name.clear();
    m_replace.reset();
    m_error.clear();
    fs::path start = m_options.start;
    while (m_options.folder && !start.empty() && !IsDirectory(start) && start.has_relative_path()) {
        start = start.parent_path();
    }
    if (!start.empty() && !IsDirectory(start)) {
        m_name = start.filename().string();
        start = start.parent_path();
    }
    if (!IsDirectory(start)) {
        start = !m_recent.empty() && IsDirectory(m_recent.front()) ? m_recent.front()
                                                                   : fs::current_path();
    }
    Navigate(start);
}

bool FileDialog::Navigate(const fs::path& directory) {
    std::error_code error;
    const fs::path absolute = fs::absolute(directory, error).lexically_normal();
    if (error || !IsDirectory(absolute)) {
        m_error = "Not a folder: " + directory.string();
        return false;
    }
    try {
        m_entries = ListDirectory(absolute, Filter(), m_showHidden);
    } catch (const fs::filesystem_error& failure) {
        m_error = "Can't read " + absolute.string() + ": " + failure.code().message();
        return false;
    }
    m_directory = absolute;
    m_pathField = m_directory.string();
    m_replace.reset();
    m_error.clear();
    return true;
}

const FileFilter* FileDialog::Filter() const {
    return m_filter < m_options.filters.size() ? &m_options.filters[m_filter] : nullptr;
}

bool FileDialog::Accept(const std::string& name) {
    if (!m_open) {
        return false;
    }
    const std::string& typed = name.empty() ? m_name : name;
    if (m_options.folder) {
        const bool blank =
            std::ranges::all_of(typed, [](unsigned char c) { return std::isspace(c) != 0; });
        const fs::path path =
            blank ? m_directory
                  : (fs::path(typed).is_absolute() ? fs::path(typed) : m_directory / typed)
                        .lexically_normal();
        if (!IsDirectory(path)) {
            m_error = "No such folder: " + path.string();
            return false;
        }
        Choose(path);
        return true;
    }
    if (std::ranges::all_of(typed, [](unsigned char c) { return std::isspace(c) != 0; })) {
        m_error = m_options.save ? "Type a file name" : "Choose a file";
        return false;
    }
    fs::path path =
        (fs::path(typed).is_absolute() ? fs::path(typed) : m_directory / typed).lexically_normal();
    if (IsDirectory(path)) {
        if (Navigate(path)) {
            m_name.clear();
        }
        return false;
    }
    const FileFilter* filter = Filter();
    if (m_options.save && filter != nullptr && !filter->extensions.empty() &&
        !path.has_extension()) {
        path += filter->extensions.front();
    }
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (!m_options.save) {
        if (!exists) {
            m_error = "No such file: " + path.string();
            return false;
        }
        Choose(path);
        return true;
    }
    if (!IsDirectory(path.parent_path())) {
        m_error = "No such folder: " + path.parent_path().string();
        return false;
    }
    if (exists && m_replace != path) {
        m_replace = path;
        m_error.clear();
        return false;
    }
    Choose(path);
    return true;
}

void FileDialog::Cancel() {
    m_open = false;
    m_done = nullptr;
    m_replace.reset();
}

void FileDialog::Choose(fs::path path) {
    const fs::path folder = m_options.folder ? path : path.parent_path();
    std::erase(m_recent, folder);
    m_recent.insert(m_recent.begin(), folder);
    if (m_recent.size() > kRecentFolders) {
        m_recent.resize(kRecentFolders);
    }
    SavePlaces();
    m_open = false;
    m_replace.reset();
    const Done done = std::exchange(m_done, nullptr);
    if (done) {
        done(path);
    }
}

void FileDialog::Draw() {
    if (!m_open) {
        return;
    }
    const std::string id = m_options.title + "###chrono_batch_render_file_dialog";
    if (m_popupRequested) {
        ImGui::OpenPopup(id.c_str());
        m_popupRequested = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(
        {std::min(60 * em, viewport->Size.x * 0.9f), std::min(38 * em, viewport->Size.y * 0.9f)},
        ImGuiCond_Appearing);
    bool keep = true;
    if (!ImGui::BeginPopupModal(id.c_str(), &keep)) {
        if (!keep) {
            Cancel();
        }
        return;
    }
    if (ImGui::Button(icons::kArrowUp)) {
        const fs::path parent = m_directory.parent_path();
        if (parent != m_directory) {
            Navigate(parent);
        }
    }
    ImGui::SetItemTooltip("Up a folder");
    ImGui::SameLine();
    const bool favorite = std::ranges::find(m_favorites, m_directory) != m_favorites.end();
    ImGui::PushStyleColor(ImGuiCol_Text, favorite ? ImVec4(0.95f, 0.72f, 0.18f, 1.0f)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button(icons::kStar)) {
        ToggleFavorite(m_directory);
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip(favorite ? "Remove this folder from the favorites"
                                   : "Add this folder to the favorites");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", &m_pathField, ImGuiInputTextFlags_EnterReturnsTrue) &&
        !Navigate(m_pathField)) {
        m_pathField = m_directory.string();
    }
    const float footer =
        ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    DrawPlaces(footer);
    ImGui::SameLine();
    bool accepted = false;
    if (const std::optional<fs::path> enter = DrawEntries(footer, accepted)) {
        if (Navigate(*enter) && m_options.folder) {
            m_name.clear();
        }
    }
    DrawFooter(accepted);
    if (accepted && m_open) {
        Accept();
    }
    if (!m_open) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileDialog::DrawPlaces(float footer) {
    if (ImGui::BeginChild("##places", {12 * ImGui::GetFontSize(), -footer},
                          ImGuiChildFlags_Borders)) {
        std::optional<fs::path> unfavorite;
        std::optional<fs::path> favorite;
        std::optional<fs::path> forget;
        const auto place = [&](const char* icon, const std::string& label, const fs::path& path) {
            if (path.empty()) {
                return false;
            }
            const std::string text = Labelled(icon, label) + "##" + path.string();
            if (ImGui::Selectable(text.c_str(), path == m_directory)) {
                Navigate(path);
            }
            ImGui::SetItemTooltip("%s", path.string().c_str());
            return ImGui::BeginPopupContextItem();
        };
        place(icons::kHome, "Home", HomeDirectory());
        place(icons::kFolderOpen, "Working folder", fs::current_path());
        place(icons::kHardDrive, "Root", m_directory.root_path());
        ImGui::SeparatorText("Favorites");
        if (m_favorites.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Star a folder, or right-click one, to add it here");
            ImGui::PopTextWrapPos();
        }
        for (const fs::path& folder : m_favorites) {
            if (place(icons::kStar, FolderName(folder), folder)) {
                if (ImGui::MenuItem("Remove from Favorites")) {
                    unfavorite = folder;
                }
                ImGui::EndPopup();
            }
        }
        if (!m_recent.empty()) {
            ImGui::SeparatorText("Recent");
            for (const fs::path& folder : m_recent) {
                if (place(icons::kHistory, FolderName(folder), folder)) {
                    const bool known = std::ranges::find(m_favorites, folder) != m_favorites.end();
                    if (ImGui::MenuItem("Add to Favorites", nullptr, false, !known)) {
                        favorite = folder;
                    }
                    if (ImGui::MenuItem("Forget")) {
                        forget = folder;
                    }
                    ImGui::EndPopup();
                }
            }
        }
        if (unfavorite || favorite) {
            ToggleFavorite(unfavorite ? *unfavorite : *favorite);
        }
        if (forget) {
            Forget(*forget);
        }
    }
    ImGui::EndChild();
}

std::optional<fs::path> FileDialog::DrawEntries(float footer, bool& accepted) {
    std::optional<fs::path> enter;
    std::optional<fs::path> toggled;
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##entries", 3, kTable, {0, -footer})) {
        return enter;
    }
    const float em = ImGui::GetFontSize();
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 6 * em);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 9 * em);
    ImGui::TableHeadersRow();
    for (const DirectoryEntry& entry : m_entries) {
        if (m_options.folder && !entry.directory) {
            continue;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string label =
            Labelled(entry.directory ? icons::kFolder : icons::kFile, entry.name);
        const bool selected = (m_options.folder || !entry.directory) && entry.name == m_name;
        if (ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
            const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (entry.directory) {
                if (twice) {
                    enter = m_directory / entry.name;
                } else if (m_options.folder) {
                    m_name = entry.name;
                }
            } else {
                m_name = entry.name;
                m_replace.reset();
                accepted = twice;
            }
        }
        if (entry.directory && ImGui::BeginPopupContextItem()) {
            const fs::path folder = m_directory / entry.name;
            const bool favorite = std::ranges::find(m_favorites, folder) != m_favorites.end();
            if (ImGui::MenuItem(favorite ? "Remove from Favorites" : "Add to Favorites")) {
                toggled = folder;
            }
            ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        if (!entry.directory) {
            ImGui::TextDisabled("%s", FormatBytes(entry.size).c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TimeText(entry.modified).c_str());
    }
    ImGui::EndTable();
    if (toggled) {
        ToggleFavorite(*toggled);
    }
    return enter;
}

void FileDialog::DrawFooter(bool& accepted) {
    const float em = ImGui::GetFontSize();
    const float buttons = 12 * em;
    const float types = m_options.filters.empty() ? 0.0f : 14 * em;
    ImGui::SetNextItemWidth(-(buttons + types + ImGui::GetStyle().ItemSpacing.x * 2.0f));
    if (ImGui::InputTextWithHint("##name",
                                 m_options.folder ? "Folder (blank for this one)" : "File name",
                                 &m_name, ImGuiInputTextFlags_EnterReturnsTrue)) {
        accepted = true;
    }
    if (!m_options.filters.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(types);
        if (ImGui::BeginCombo("##type", m_options.filters[m_filter].label.c_str())) {
            for (size_t i = 0; i < m_options.filters.size(); ++i) {
                if (ImGui::Selectable(m_options.filters[i].label.c_str(), i == m_filter)) {
                    m_filter = i;
                    Navigate(m_directory);
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    const char* action = m_options.folder ? "Select" : m_options.save ? "Save" : "Open";
    if (ImGui::Button(action, {buttons * 0.5f - 4.0f, 0})) {
        accepted = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {-1, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        Cancel();
    }
    if (ImGui::Checkbox("Hidden files", &m_showHidden)) {
        Navigate(m_directory);
    }
    ImGui::SameLine();
    if (m_replace) {
        ImGui::TextColored({0.91f, 0.70f, 0.24f, 1}, "%s exists. Replace it?",
                           m_replace->filename().string().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Replace")) {
            Choose(*m_replace);
        }
    } else if (!m_error.empty()) {
        ImGui::TextColored({0.89f, 0.34f, 0.32f, 1}, "%s", m_error.c_str());
    }
}

}
