// SPDX-License-Identifier: BSD-3-Clause
#include "chrono_batch_render/app/viewer/widgets/import_dialog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>
#include <variant>

#include <imgui.h>
#include <imgui_stdlib.h>

namespace chrono::batch_render::app {
namespace {

constexpr const char* kPopup = "Import##import dialog";
constexpr const char* kUpAxes[] = {"From the file", "Y", "Z"};

void Tip(const std::string& tip) {
    if (!tip.empty()) {
        ImGui::SetItemTooltip("%s", tip.c_str());
    }
}

}

std::string ImportName(const std::filesystem::path& file) {
    std::string name = file.stem().string();
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) {
        name.insert(name.begin(), '_');
    }
    return name;
}

bool ImportDialog::Open(std::filesystem::path file, std::string path, Done done) {
    std::vector<std::shared_ptr<Importer>> importers = FindImporters(file.extension().string());
    if (importers.empty()) {
        return false;
    }
    m_file = std::move(file);
    m_path = std::move(path);
    m_done = std::move(done);
    m_importers = std::move(importers);
    m_up = 0;
    m_scaled = false;
    m_metersPerUnit = 1;
    Choose(0);
    m_open = true;
    m_popupRequested = true;
    return true;
}

void ImportDialog::Choose(size_t importer) {
    m_chosen = importer;
    m_options = m_importers[importer]->Options();
    m_values.clear();
    for (const ImportOption& option : m_options) {
        m_values[option.name] = option.value;
    }
}

void ImportDialog::DrawOption(const ImportOption& option) {
    ImportValue& value = m_values[option.name];
    const std::string label = option.label.empty() ? option.name : option.label;
    const std::string id = "##" + option.name;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    Tip(option.tip);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    if (bool* on = std::get_if<bool>(&value)) {
        ImGui::Checkbox(id.c_str(), on);
    } else if (int64_t* number = std::get_if<int64_t>(&value)) {
        ImGui::InputScalar(id.c_str(), ImGuiDataType_S64, number);
    } else if (double* real = std::get_if<double>(&value)) {
        ImGui::InputDouble(id.c_str(), real, 0, 0, "%.6g");
    } else if (std::string* text = std::get_if<std::string>(&value)) {
        if (option.choices.empty()) {
            ImGui::InputText(id.c_str(), text);
        } else if (ImGui::BeginCombo(id.c_str(), text->c_str())) {
            for (const std::string& choice : option.choices) {
                if (ImGui::Selectable(choice.c_str(), choice == *text)) {
                    *text = choice;
                }
            }
            ImGui::EndCombo();
        }
    }
    Tip(option.tip);
}

void ImportDialog::Draw() {
    if (!m_open) {
        return;
    }
    if (m_popupRequested) {
        ImGui::OpenPopup(kPopup);
        m_popupRequested = false;
    }
    const float font = ImGui::GetFontSize();
    ImGui::SetNextWindowSize({30 * font, 0}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            {0.5f, 0.5f});
    bool open = true;
    if (!ImGui::BeginPopupModal(kPopup, &open)) {
        m_open = false;
        return;
    }
    ImGui::TextUnformatted(m_file.filename().string().c_str());
    ImGui::SetItemTooltip("%s", m_file.string().c_str());
    ImGui::Separator();
    if (ImGui::BeginTable("##import options", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 9 * font);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Importer");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        if (m_importers.size() == 1) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(m_importers.front()->Name().c_str());
        } else if (ImGui::BeginCombo("##importer", m_importers[m_chosen]->Name().c_str())) {
            for (size_t at = 0; at < m_importers.size(); ++at) {
                const std::string name = m_importers[at]->Name() + (at == 0 ? " (default)" : "");
                if (ImGui::Selectable(name.c_str(), at == m_chosen) && at != m_chosen) {
                    Choose(at);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Prim Path");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##path", &m_path);
        ImGui::SetItemTooltip("The prim the file is imported at");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Up Axis");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##up", &m_up, kUpAxes, int(std::size(kUpAxes)));
        ImGui::SetItemTooltip("Which axis is up in the file, for formats that don't say");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Meters per Unit");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##scaled", &m_scaled);
        ImGui::SetItemTooltip("Override the file's units");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(!m_scaled);
        ImGui::InputFloat("##meters", &m_metersPerUnit, 0, 0, "%.6g");
        ImGui::EndDisabled();

        for (const ImportOption& option : m_options) {
            DrawOption(option);
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    const bool valid = m_path.size() > 1 && m_path.front() == '/' && m_path.back() != '/' &&
                       (!m_scaled || m_metersPerUnit > 0);
    const float button = 6 * font;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2 * button -
                         ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", {button, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        open = false;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Import", {button, 0})) {
        ImportDesc desc{.importer = m_importers[m_chosen], .options = m_values};
        if (m_up > 0) {
            desc.up = m_up == 1 ? UpAxis::Y : UpAxis::Z;
        }
        if (m_scaled) {
            desc.metersPerUnit = m_metersPerUnit;
        }
        m_done(m_path, m_file, desc);
        open = false;
    }
    ImGui::EndDisabled();
    if (!open) {
        m_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}
