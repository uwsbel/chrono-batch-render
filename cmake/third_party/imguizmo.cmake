# SPDX-License-Identifier: BSD-3-Clause
FetchContent_Declare(
    imguizmo
    URL
        https://github.com/CedricGuillemet/ImGuizmo/archive/refs/tags/1.10.tar.gz
    URL_HASH SHA256=a467155ee60a262430478a06c6534ab56fe62b053b896308581f5aa7057f5094
    SOURCE_SUBDIR no-build
)
FetchContent_MakeAvailable(imguizmo)

add_library(chrono_batch_render_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(chrono_batch_render_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(chrono_batch_render_imguizmo PUBLIC chrono_batch_render_imgui)
target_compile_definitions(chrono_batch_render_imguizmo PRIVATE chrono_batch_render_viewer_EXPORTS)
target_compile_options(chrono_batch_render_imguizmo PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
