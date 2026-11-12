// SPDX-License-Identifier: BSD-3-Clause
#include <string>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "bindings.h"
#include "chrono_batch_render/app/cli.h"
#include "chrono_batch_render/stage/store_lock.h"

#ifdef Py_LIMITED_API
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace {

bool HoldsTheGil() {
#ifdef Py_LIMITED_API
    using Check = int (*)();
    static const Check check = [] {
#if defined(_WIN32)
        HMODULE python = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Py_IsInitialized), &python);
        return python ? reinterpret_cast<Check>(GetProcAddress(python, "PyGILState_Check"))
                      : nullptr;
#else
        return reinterpret_cast<Check>(dlsym(RTLD_DEFAULT, "PyGILState_Check"));
#endif
    }();
    return check != nullptr && check() != 0;
#else
    return PyGILState_Check() != 0;
#endif
}

void WaitWithoutTheGil(void (*wait)(void*), void* context) {
    if (Py_IsInitialized() && HoldsTheGil()) {
        const nanobind::gil_scoped_release release;
        wait(context);
        return;
    }
    wait(context);
}

}

NB_MODULE(_core, module) {
    module.attr("__version__") = CHRONO_BATCH_RENDER_VERSION;
    chrono::batch_render::StoreLock::SetBlockingWait(WaitWithoutTheGil);
    chrono::batch_render::python::BindDevice(module);
    chrono::batch_render::python::BindScene(module);
    chrono::batch_render::python::BindRender(module);
    chrono::batch_render::python::BindSensorPipeline(module);
    chrono::batch_render::python::BindImport(module);
    chrono::batch_render::python::BindIo(module);
    chrono::batch_render::python::BindRotation(module);
    module.def(
        "run_cli",
        [](const std::vector<std::string>& args) {
            const nanobind::gil_scoped_release release;
            return chrono::batch_render::app::RunCli(args);
        },
        nanobind::arg("args"));
}
