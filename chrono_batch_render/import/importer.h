// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "chrono_batch_render/export.h"
#include "chrono_batch_render/scene/prim.h"
#include "chrono_batch_render/scene/scene.h"
#include "chrono_batch_render/util/error.h"

namespace chrono::batch_render {

class CHRONO_BATCH_RENDER_API CancelledError : public Error {
  public:
    using Error::Error;
    ~CancelledError() override;
};

struct CHRONO_BATCH_RENDER_API ImportProgress {
    std::string stage;
    uint64_t done = 0;
    uint64_t total = 0;
};

using ImportProgressCallback = std::function<void(const ImportProgress& progress)>;

class ImportState;

enum class UpAxis : uint8_t { Y, Z };

using ImportValue = std::variant<bool, int64_t, double, std::string>;
using ImportValues = std::map<std::string, ImportValue, std::less<>>;

struct CHRONO_BATCH_RENDER_API ImportOption {
    std::string name;
    std::string label;
    std::string tip;
    ImportValue value = false;
    std::vector<std::string> choices;
};

class CHRONO_BATCH_RENDER_API ImportContext {
  public:
    ImportContext();
    explicit ImportContext(std::shared_ptr<ImportState> state);

    void Report(std::string_view stage, uint64_t done, uint64_t total);
    bool Cancelled() const;
    std::optional<UpAxis> Up() const;
    std::optional<float> MetersPerUnit() const;
    std::optional<ImportValue> Option(std::string_view name) const;

  private:
    std::shared_ptr<ImportState> m_state;
};

struct CHRONO_BATCH_RENDER_API ImportSource {
    std::filesystem::path file;
    std::map<std::string, FileBytes, std::less<>> files;

    std::filesystem::path Locate(std::string_view name) const;
    FileBytes Find(std::string_view name) const;
    FileBytes Read(std::string_view name) const;
    FileBytes ReadEntry() const;
};

class CHRONO_BATCH_RENDER_API ImportPlan {
  public:
    using DefineStep = std::function<void(Scene& scene, std::string_view path)>;

    template <class T> void Define(std::string path, typename T::Desc desc = {}) {
        Add(std::move(path), [desc = std::move(desc)](Scene& scene, std::string_view at) {
            scene.Define<T>(at, desc);
        });
    }

    void Add(std::string path, DefineStep define);
    std::string AddFile(std::string_view name, FileBytes bytes);
    uint32_t PrimCount() const;
    Prim Apply(Scene& scene, std::string_view root) const;

  private:
    struct Step {
        std::string path;
        DefineStep define;
    };

    std::vector<Step> m_steps;
    std::vector<std::pair<std::string, FileBytes>> m_files;
    uint64_t m_id = 0;
};

class CHRONO_BATCH_RENDER_API Importer {
  public:
    virtual ~Importer();

    virtual std::string Name() const = 0;
    virtual std::vector<std::string> Extensions() const = 0;
    virtual std::vector<ImportOption> Options() const;
    virtual ImportPlan Read(std::string_view path, const ImportSource& source,
                            ImportContext& context) = 0;
};

struct CHRONO_BATCH_RENDER_API ImportReader {
    std::string name;
    std::function<ImportPlan(ImportContext& context)> read;
};

struct CHRONO_BATCH_RENDER_API ImportDesc {
    std::shared_ptr<Importer> importer;
    ImportProgressCallback progress;
    bool synchronous = false;
    std::optional<UpAxis> up;
    std::optional<float> metersPerUnit;
    ImportValues options;
};

class CHRONO_BATCH_RENDER_API ImportJob {
  public:
    ImportJob() = default;

    bool Ready() const;
    ImportProgress Progress() const;
    void Cancel();
    Prim Wait();

  private:
    friend class Scene;

    ImportJob(Ref<Scene> scene, std::shared_ptr<ImportState> state)
        : m_scene(std::move(scene)), m_state(std::move(state)) {}

    Ref<Scene> m_scene;
    std::shared_ptr<ImportState> m_state;
};

CHRONO_BATCH_RENDER_API void RegisterImporter(std::shared_ptr<Importer> importer);
CHRONO_BATCH_RENDER_API void UnregisterImporter(std::string_view extension,
                                                std::string_view name = {});
CHRONO_BATCH_RENDER_API std::shared_ptr<Importer> FindImporter(std::string_view extension);
CHRONO_BATCH_RENDER_API std::vector<std::shared_ptr<Importer>>
FindImporters(std::string_view extension);
CHRONO_BATCH_RENDER_API std::vector<std::string> ImporterExtensions();

}
