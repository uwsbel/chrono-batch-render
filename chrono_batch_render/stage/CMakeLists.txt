# SPDX-License-Identifier: BSD-3-Clause
chrono_batch_render_object_library(
    chrono_batch_render_stage
    columns/array_pool.cpp
    columns/column_store.cpp
    columns/column_table.cpp
    columns/device_heap.cpp
    publish/change_set.cpp
    publish/publisher.cpp
    schema/runtime.cpp
    store_lock.cpp
    structure/packing.cpp
    structure/paths.cpp
    structure/prims.cpp
    structure/structure.cpp
    structure/tokens.cpp
    views/attribute.cpp
    views/layout.cpp
    views/mapping.cpp
    views/poses.cpp
    views/staged.cpp
    views/view.cpp
)
target_link_libraries(chrono_batch_render_stage PUBLIC chrono_batch_render_device)
chrono_batch_render_embed(
    chrono_batch_render_stage
    LIBRARY kStageKernels
    USES
        kDeviceKernels
    HEADER chrono_batch_render/stage/kernels.h
    ROOTS
        "${CMAKE_CURRENT_SOURCE_DIR}/columns/kernels|chrono_batch_render/stage/columns/kernels/|*.slang"
        "${CMAKE_CURRENT_SOURCE_DIR}/columns/kernels|chrono_batch_render/stage/columns/kernels/|*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema|chrono_batch_render/stage/schema/|*.h"
        "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/chrono_batch_render/scene/schema|chrono_batch_render/scene/schema/|*.h"
)

file(GLOB_RECURSE chrono_batch_render_stage_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(FILTER chrono_batch_render_stage_files EXCLUDE REGEX "/CMakeLists\\.txt$")
chrono_batch_render_check_layer(
    chrono_batch_render_stage
    ALLOW
        "^chrono_batch_render/device/"
        "^chrono_batch_render/util/"
        "^chrono_batch_render/stage/"
        "^chrono_batch_render/export\\.h$"
        "^chrono_batch_render/scene/schema/(columns|registry)\\.h$"
    FILES
        ${chrono_batch_render_stage_files}
)

chrono_batch_render_static_library(
    chrono_batch_render_stage_internal
    chrono_batch_render_stage
    chrono_batch_render_device_internal
)

chrono_batch_render_public_files(
    columns/kernels/paged.h
    primvar.h
    schema/ids.h
    schema/primvars.h
    schema/runtime.h
    schema/schema.h
    schema/types.h
    structure/tokens.h
    types.h
    views/attribute.h
    views/layout.h
    views/mapping.h
    views/poses.h
    views/view.h
)
