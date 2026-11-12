// SPDX-License-Identifier: BSD-3-Clause
#include <exception>
#include <filesystem>
#include <memory>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "chrono_batch_render/app/viewer/panels/panels.h"
#include "chrono_batch_render/util/log.h"

namespace chrono::batch_render::app {
namespace {

#ifdef CHRONO_BATCH_RENDER_VIEWER_XR_FILE
std::filesystem::path ThisLibrary() {
#if defined(_WIN32)
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ThisLibrary), &self)) {
        return {};
    }
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(path);
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&ThisLibrary), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path path = std::filesystem::canonical(info.dli_fname, error);
    return error ? std::filesystem::path() : path;
#endif
}

void* FindAddXrPanel() {
    const std::filesystem::path self = ThisLibrary();
    if (self.filename() != std::filesystem::path(CHRONO_BATCH_RENDER_VIEWER_FILE)) {
        return nullptr;
    }
    const std::filesystem::path library = self.parent_path() / CHRONO_BATCH_RENDER_VIEWER_XR_FILE;
#if defined(_WIN32)
    const HMODULE module =
        LoadLibraryExW(library.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    return module ? reinterpret_cast<void*>(GetProcAddress(module, kAddXrPanel)) : nullptr;
#else
    void* module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        Log(LogLevel::Debug, "the viewer has no XR: {}", dlerror());
        return nullptr;
    }
    return dlsym(module, kAddXrPanel);
#endif
}
#endif

}

std::shared_ptr<XrPanel> LoadXrPanel([[maybe_unused]] Viewer& viewer,
                                     [[maybe_unused]] const Ref<Scene>& scene) {
#ifdef CHRONO_BATCH_RENDER_VIEWER_XR_FILE
    static void* const entry = FindAddXrPanel();
    if (entry == nullptr) {
        return nullptr;
    }
    std::shared_ptr<XrPanel> panel;
    reinterpret_cast<AddXrPanelEntry>(entry)(&viewer, &scene, &panel);
    return panel;
#else
    return nullptr;
#endif
}

}
