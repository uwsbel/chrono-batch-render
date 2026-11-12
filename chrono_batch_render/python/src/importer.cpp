// SPDX-License-Identifier: BSD-3-Clause
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>
#include <nanobind/trampoline.h>

#include "bindings.h"
#include "values.h"

namespace nb = nanobind;

namespace chrono::batch_render::python {
namespace {

class PyImporter : public Importer {
  public:
    NB_TRAMPOLINE(Importer);

    std::string Name() const override {
        NB_OVERRIDE_PURE_NAME("name", Name);
    }

    std::vector<std::string> Extensions() const override {
        NB_OVERRIDE_PURE_NAME("extensions", Extensions);
    }

    std::vector<ImportOption> Options() const override {
        NB_OVERRIDE_NAME("options", Options);
    }

    ImportPlan Read(std::string_view path, const ImportSource& source,
                    ImportContext& context) override {
        nb::detail::ticket ticket(nb_trampoline, "read", nb::detail::str_hash("read"), true);
        return nb::cast<ImportPlan>(nb_trampoline.base().attr(ticket.key)(
            path, source, nb::cast(context, nb::rv_policy::copy)));
    }
};

std::shared_ptr<nb::object> Hold(nb::object object) {
    return std::shared_ptr<nb::object>(new nb::object(std::move(object)), [](nb::object* held) {
        if (Py_IsInitialized()) {
            nb::gil_scoped_acquire gil;
            delete held;
        }
    });
}

FileBytes ToFileBytes(const nb::bytes& bytes) {
    const auto* data = static_cast<const std::byte*>(bytes.data());
    return std::make_shared<const std::vector<std::byte>>(data, data + bytes.size());
}

nb::object ToPython(const FileBytes& bytes) {
    if (!bytes) {
        return nb::none();
    }
    return nb::bytes(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

using OptionValues = std::optional<std::map<std::string, ImportValue>>;

ImportDesc CreateImportDesc(std::optional<std::shared_ptr<Importer>> importer,
                            std::optional<nb::callable> progress, bool synchronous,
                            std::optional<UpAxis> up, std::optional<float> metersPerUnit,
                            OptionValues options) {
    ImportDesc desc{.synchronous = synchronous, .up = up, .metersPerUnit = metersPerUnit};
    if (options) {
        desc.options.insert(options->begin(), options->end());
    }
    if (importer) {
        desc.importer = std::move(*importer);
    }
    if (progress) {
        desc.progress = [callback = Hold(std::move(*progress))](const ImportProgress& report) {
            nb::gil_scoped_acquire gil;
            (*callback)(nb::cast(report, nb::rv_policy::copy));
        };
    }
    return desc;
}

void BindSceneImports() {
    const nb::handle sceneType = nb::type<Scene>();
    const auto import = [](Scene& scene, std::string_view path, const ImportSource& source,
                           std::optional<std::shared_ptr<Importer>> importer,
                           std::optional<nb::callable> progress, bool synchronous,
                           std::optional<UpAxis> up, std::optional<float> metersPerUnit,
                           OptionValues options) {
        const ImportDesc desc =
            CreateImportDesc(std::move(importer), std::move(progress), synchronous, up,
                             metersPerUnit, std::move(options));
        nb::gil_scoped_release release;
        return scene.Import(path, source, desc);
    };
    nb::cpp_function_def(
        [import](Scene& scene, std::string_view path, const std::filesystem::path& file,
                 std::optional<std::shared_ptr<Importer>> importer,
                 std::optional<nb::callable> progress, bool synchronous, std::optional<UpAxis> up,
                 std::optional<float> metersPerUnit, OptionValues options) {
            return import(scene, path, ImportSource{.file = file}, std::move(importer),
                          std::move(progress), synchronous, up, metersPerUnit, std::move(options));
        },
        nb::is_method(), nb::scope(sceneType), nb::name("import_file"), nb::arg("path"),
        nb::arg("file"), nb::kw_only(), nb::arg("importer") = nb::none(),
        nb::arg("progress") = nb::none(), nb::arg("synchronous") = false,
        nb::arg("up") = nb::none(), nb::arg("meters_per_unit") = nb::none(),
        nb::arg("options") = nb::none());
    nb::cpp_function_def(import, nb::is_method(), nb::scope(sceneType), nb::name("import_file"),
                         nb::arg("path"), nb::arg("source"), nb::kw_only(),
                         nb::arg("importer") = nb::none(), nb::arg("progress") = nb::none(),
                         nb::arg("synchronous") = false, nb::arg("up") = nb::none(),
                         nb::arg("meters_per_unit") = nb::none(), nb::arg("options") = nb::none());
    nb::cpp_function_def(
        [](Scene& scene, std::string_view path, nb::callable read, std::string name,
           std::optional<nb::callable> progress, bool synchronous, std::optional<UpAxis> up,
           std::optional<float> metersPerUnit, OptionValues options) {
            ImportReader reader{.name = std::move(name),
                                .read = [held = Hold(std::move(read))](ImportContext& context) {
                                    nb::gil_scoped_acquire gil;
                                    return nb::cast<ImportPlan>(
                                        (*held)(nb::cast(context, nb::rv_policy::copy)));
                                }};
            const ImportDesc desc = CreateImportDesc(std::nullopt, std::move(progress), synchronous,
                                                     up, metersPerUnit, std::move(options));
            nb::gil_scoped_release release;
            return scene.Import(path, reader, desc);
        },
        nb::is_method(), nb::scope(sceneType), nb::name("import_from"), nb::arg("path"),
        nb::arg("read"), nb::kw_only(), nb::arg("name") = "a Python reader",
        nb::arg("progress") = nb::none(), nb::arg("synchronous") = false,
        nb::arg("up") = nb::none(), nb::arg("meters_per_unit") = nb::none(),
        nb::arg("options") = nb::none());
    nb::cpp_function_def(
        [](Scene& scene, std::string path, const nb::bytes& bytes, std::optional<ScenePrim> owner) {
            scene.AddFile(std::move(path), ToFileBytes(bytes),
                          owner ? std::optional<Prim>(owner->prim) : std::nullopt);
        },
        nb::is_method(), nb::scope(sceneType), nb::name("add_file"), nb::arg("path"),
        nb::arg("bytes"), nb::kw_only(), nb::arg("owner") = nb::none());
    nb::cpp_function_def(
        [](Scene& scene, std::string path,
           const nb::ndarray<nb::ro, nb::device::cpu, nb::c_contig>& array,
           std::optional<ScenePrim> owner) {
            if (array.ndim() != 2 && array.ndim() != 3) {
                throw nb::value_error("Textures are (rows, cols[, channels]) arrays");
            }
            TextureData data{.width = uint32_t(array.shape(1)),
                             .height = uint32_t(array.shape(0)),
                             .channels = array.ndim() == 3 ? uint32_t(array.shape(2)) : 1u};
            const auto* bytes = static_cast<const std::byte*>(array.data());
            std::vector<float> converted;
            if (array.dtype() == nb::dtype<uint8_t>()) {
                data.type = TexelType::Uint8;
                data.texels = Span<const std::byte>(bytes, array.nbytes());
            } else if (array.dtype() == nb::dtype<float>()) {
                data.texels = Span<const std::byte>(bytes, array.nbytes());
            } else if (array.dtype() == nb::dtype<double>()) {
                const auto* values = static_cast<const double*>(array.data());
                converted.resize(array.size());
                for (size_t at = 0; at < converted.size(); ++at) {
                    converted[at] = float(values[at]);
                }
                data.texels =
                    Span<const std::byte>(reinterpret_cast<const std::byte*>(converted.data()),
                                          converted.size() * sizeof(float));
            } else {
                throw nb::type_error("Textures are uint8, float32 or float64 arrays");
            }
            scene.AddTexture(std::move(path), data,
                             owner ? std::optional<Prim>(owner->prim) : std::nullopt);
        },
        nb::is_method(), nb::scope(sceneType), nb::name("add_texture"), nb::arg("path"),
        nb::arg("texels"), nb::kw_only(), nb::arg("owner") = nb::none());
    nb::cpp_function_def(&Scene::RemoveFile, nb::is_method(), nb::scope(sceneType),
                         nb::name("remove_file"), nb::arg("path"));
}

}

void BindImport(nb::module_& module) {
    nb::exception<CancelledError>(module, "CancelledError", PyExc_RuntimeError);

    nb::class_<ImportProgress>(module, "ImportProgress")
        .def_ro("stage", &ImportProgress::stage)
        .def_ro("done", &ImportProgress::done)
        .def_ro("total", &ImportProgress::total)
        .def("__repr__", [](const ImportProgress& progress) {
            return fmt::format("ImportProgress({}, {}/{})", progress.stage, progress.done,
                               progress.total);
        });

    nb::enum_<UpAxis>(module, "UpAxis").value("Y", UpAxis::Y).value("Z", UpAxis::Z);

    nb::class_<ImportContext>(module, "ImportContext")
        .def(nb::init<>())
        .def("report", &ImportContext::Report, nb::arg("stage"), nb::arg("done"), nb::arg("total"))
        .def_prop_ro("cancelled", &ImportContext::Cancelled)
        .def_prop_ro("up", &ImportContext::Up)
        .def_prop_ro("meters_per_unit", &ImportContext::MetersPerUnit)
        .def("option", &ImportContext::Option, nb::arg("name"));

    nb::class_<ImportOption>(module, "ImportOption")
        .def(
            "__init__",
            [](ImportOption* option, std::string name, ImportValue value, std::string label,
               std::string tip, std::vector<std::string> choices) {
                new (option) ImportOption{.name = std::move(name),
                                          .label = std::move(label),
                                          .tip = std::move(tip),
                                          .value = std::move(value),
                                          .choices = std::move(choices)};
            },
            nb::arg("name"), nb::arg("value"), nb::kw_only(), nb::arg("label") = "",
            nb::arg("tip") = "", nb::arg("choices") = std::vector<std::string>{})
        .def_rw("name", &ImportOption::name)
        .def_rw("label", &ImportOption::label)
        .def_rw("tip", &ImportOption::tip)
        .def_rw("value", &ImportOption::value)
        .def_rw("choices", &ImportOption::choices)
        .def("__repr__", [](const ImportOption& option) {
            return fmt::format("ImportOption({})", option.name);
        });

    nb::class_<ImportSource>(module, "ImportSource")
        .def(
            "__init__",
            [](ImportSource* source, std::filesystem::path file,
               std::optional<std::map<std::string, nb::bytes>> files) {
                new (source) ImportSource{.file = std::move(file)};
                for (const auto& [name, bytes] :
                     files.value_or(std::map<std::string, nb::bytes>{})) {
                    source->files[name] = ToFileBytes(bytes);
                }
            },
            nb::arg("file"), nb::arg("files") = nb::none())
        .def_rw("file", &ImportSource::file)
        .def_prop_ro("names",
                     [](const ImportSource& source) {
                         std::vector<std::string> names;
                         for (const auto& [name, bytes] : source.files) {
                             names.push_back(name);
                         }
                         return names;
                     })
        .def("locate", &ImportSource::Locate, nb::arg("name"))
        .def(
            "find",
            [](const ImportSource& source, std::string_view name) {
                return ToPython(source.Find(name));
            },
            nb::arg("name"))
        .def(
            "read",
            [](const ImportSource& source, std::string_view name) {
                return ToPython(source.Read(name));
            },
            nb::arg("name"))
        .def("read_entry", [](const ImportSource& source) { return ToPython(source.ReadEntry()); });

    nb::class_<ImportPlan>(module, "ImportPlan")
        .def(nb::init<>())
        .def(
            "define",
            [](ImportPlan& plan, std::string path, std::string_view schema,
               std::optional<nb::dict> values) {
                const SchemaId id = FindSchema(schema);
                plan.Add(std::move(path),
                         [id, held = Hold(values ? std::move(*values) : nb::dict())](
                             Scene& scene, std::string_view at) {
                             nb::gil_scoped_acquire gil;
                             DefineSchema(scene, at, id, nb::cast<nb::dict>(*held), nb::dict());
                         });
            },
            nb::arg("path"), nb::arg("schema"), nb::arg("values") = nb::none())
        .def(
            "add_file",
            [](ImportPlan& plan, std::string_view name, const nb::bytes& bytes) {
                return plan.AddFile(name, ToFileBytes(bytes));
            },
            nb::arg("name"), nb::arg("bytes"))
        .def_prop_ro("prim_count", &ImportPlan::PrimCount);

    nb::class_<Importer, PyImporter>(module, "Importer")
        .def(nb::init<>())
        .def("name", &Importer::Name)
        .def("extensions", &Importer::Extensions)
        .def("options", &Importer::Options)
        .def(
            "read",
            [](Importer& importer, std::string_view path, const ImportSource& source,
               std::optional<ImportContext> context) {
                ImportContext used = context ? *context : ImportContext();
                return importer.Read(path, source, used);
            },
            nb::arg("path"), nb::arg("source"), nb::arg("context") = nb::none(),
            nb::call_guard<nb::gil_scoped_release>());

    nb::class_<ImportJob>(module, "ImportJob")
        .def_prop_ro("ready", &ImportJob::Ready)
        .def_prop_ro("progress", &ImportJob::Progress)
        .def("cancel", &ImportJob::Cancel)
        .def("wait", [](ImportJob& job) {
            const Prim root = [&] {
                nb::gil_scoped_release release;
                return job.Wait();
            }();
            return ScenePrim{Ref<Scene>(&root.GetScene()), root};
        });

    BindSceneImports();

    module.def("register_importer", &RegisterImporter, nb::arg("importer"));
    module.def("unregister_importer", &UnregisterImporter, nb::arg("extension"),
               nb::arg("name") = "");
    module.def("find_importer", &FindImporter, nb::arg("extension"));
    module.def("find_importers", &FindImporters, nb::arg("extension"));
    module.def("importer_extensions", &ImporterExtensions);

    nb::module_::import_("atexit").attr("register")(nb::cpp_function([] {
        for (const std::string& extension : ImporterExtensions()) {
            for (const std::shared_ptr<Importer>& importer : FindImporters(extension)) {
                if (nb::find(importer.get()).is_valid()) {
                    UnregisterImporter(extension, importer->Name());
                }
            }
        }
    }));
}

}
