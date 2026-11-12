// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "chrono_batch_render/import/importer.h"

namespace chrono::batch_render::app {

class ImportDialog {
  public:
    using Done = std::function<void(const std::string& path, const std::filesystem::path& file,
                                    const ImportDesc& desc)>;

    bool Open(std::filesystem::path file, std::string path, Done done);
    void Draw();

    bool IsOpen() const {
        return m_open;
    }

  private:
    void Choose(size_t importer);
    void DrawOption(const ImportOption& option);

    std::filesystem::path m_file;
    std::string m_path;
    Done m_done;
    std::vector<std::shared_ptr<Importer>> m_importers;
    size_t m_chosen = 0;
    std::vector<ImportOption> m_options;
    ImportValues m_values;
    int m_up = 0;
    bool m_scaled = false;
    float m_metersPerUnit = 1;
    bool m_open = false;
    bool m_popupRequested = false;
};

std::string ImportName(const std::filesystem::path& file);

}
