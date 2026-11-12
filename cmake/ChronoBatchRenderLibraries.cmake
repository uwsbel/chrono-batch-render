# SPDX-License-Identifier: BSD-3-Clause

function(chrono_batch_render_public_files)
    foreach(file IN LISTS ARGN)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
            message(
                FATAL_ERROR
                "chrono_batch_render_public_files names ${file}, which isn't in "
                "${CMAKE_CURRENT_SOURCE_DIR}"
            )
        endif()
        file(
            RELATIVE_PATH
            relative
            "${PROJECT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${file}"
        )
        set_property(
            GLOBAL
            APPEND
            PROPERTY
                CHRONO_BATCH_RENDER_PUBLIC_FILES
                    "${relative}"
        )
    endforeach()
endfunction()

function(chrono_batch_render_check_includes owner)
    get_property(public GLOBAL PROPERTY CHRONO_BATCH_RENDER_PUBLIC_FILES)
    foreach(file IN LISTS ARGN)
        file(
            STRINGS "${PROJECT_SOURCE_DIR}/${file}"
            includes
            REGEX "^#include \"chrono_batch_render/"
        )
        foreach(line IN LISTS includes)
            string(
                REGEX REPLACE "^#include \"([^\"]+)\".*"
                "\\1"
                included
                "${line}"
            )
            if(
                included
                    IN_LIST
                    public
                OR included
                    MATCHES
                    "^chrono_batch_render/[a-z_]*export\\.h$"
                OR EXISTS
                    "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/${included}"
                OR (
                    owner
                    AND included
                        MATCHES
                        "^${owner}/"
                )
            )
                continue()
            endif()
            message(FATAL_ERROR "${file} includes ${included}, which isn't public")
        endforeach()
    endforeach()
endfunction()

function(chrono_batch_render_check_layer layer)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "ALLOW;FILES")
    foreach(file IN LISTS ARG_FILES)
        file(
            STRINGS "${file}"
            lines
            REGEX "^(#include \"chrono_batch_render/|import chrono_batch_render\\.)"
        )
        foreach(line IN LISTS lines)
            if(line MATCHES "^#include \"([^\"]+)\"")
                set(included "${CMAKE_MATCH_1}")
            else()
                string(
                    REGEX REPLACE "^import ([a-z_.]+);.*"
                    "\\1"
                    included
                    "${line}"
                )
                string(
                    REPLACE "."
                    "/"
                    included
                    "${included}"
                )
            endif()
            set(allowed FALSE)
            foreach(pattern IN LISTS ARG_ALLOW)
                if(included MATCHES "${pattern}")
                    set(allowed TRUE)
                    break()
                endif()
            endforeach()
            if(NOT allowed)
                message(FATAL_ERROR "${file} includes ${included}, which is outside ${layer}")
            endif()
        endforeach()
    endforeach()
endfunction()

function(chrono_batch_render_check_public_files)
    get_property(public GLOBAL PROPERTY CHRONO_BATCH_RENDER_PUBLIC_FILES)
    chrono_batch_render_check_includes("" ${public})
endfunction()

function(chrono_batch_render_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Wno-missing-field-initializers
        )
    endif()
    if(CHRONO_BATCH_RENDER_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
            target_link_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
            if(APPLE)
                target_link_options(${target} PRIVATE "LINKER:-fatal_warnings")
            else()
                target_link_options(${target} PRIVATE "LINKER:--fatal-warnings")
            endif()
        endif()
    endif()
endfunction()

function(chrono_batch_render_coverage target)
    if(CHRONO_BATCH_RENDER_COVERAGE)
        target_compile_options(
            ${target}
            PRIVATE
                --coverage
                -fprofile-update=atomic
                -Og
                -g
        )
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

function(chrono_batch_render_object_library name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "EXPORTS" "")
    if(NOT ARG_EXPORTS)
        set(ARG_EXPORTS chrono_batch_render_EXPORTS)
    endif()
    add_library(${name} OBJECT ${ARG_UNPARSED_ARGUMENTS})
    target_include_directories(
        ${name}
        PUBLIC
            "${PROJECT_SOURCE_DIR}"
            "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}"
    )
    target_compile_definitions(${name} PRIVATE ${ARG_EXPORTS})
    target_link_libraries(${name} PUBLIC chrono_batch_render_fmt)
    chrono_batch_render_warnings(${name})
    chrono_batch_render_coverage(${name})
endfunction()

function(chrono_batch_render_shared_library name)
    add_library(${name} SHARED)
    target_link_libraries(
        ${name}
        PRIVATE
            ${ARGN}
        INTERFACE
            "$<BUILD_INTERFACE:chrono_batch_render_fmt>"
            "$<BUILD_INTERFACE:chrono_batch_render_dlpack>"
            "$<INSTALL_INTERFACE:chrono_batch_render::dlpack>"
    )
    target_include_directories(
        ${name}
        INTERFACE
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
            "$<BUILD_INTERFACE:${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    target_compile_features(${name} INTERFACE cxx_std_17)
    set_target_properties(
        ${name}
        PROPERTIES
            VERSION
                "${PROJECT_VERSION}"
            SOVERSION
                "${PROJECT_VERSION_MAJOR}"
            DEFINE_SYMBOL
                chrono_batch_render_EXPORTS
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(
            ${name}
            PRIVATE
                "LINKER:--exclude-libs,ALL"
                "LINKER:-Bsymbolic-functions"
        )
    endif()
    if(CHRONO_BATCH_RENDER_COVERAGE)
        target_link_options(${name} PRIVATE --coverage)
    endif()
    chrono_batch_render_warnings(${name})
endfunction()

function(chrono_batch_render_static_library name)
    add_library(${name} STATIC)
    target_link_libraries(${name} PUBLIC ${ARGN})
    target_compile_definitions(${name} INTERFACE CHRONO_BATCH_RENDER_STATIC_DEFINE)
    target_link_options(${name} INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/NOIMPLIB;/NOEXP>")
    if(CHRONO_BATCH_RENDER_COVERAGE)
        target_link_options(${name} INTERFACE --coverage)
    endif()
endfunction()

function(chrono_batch_render_embed target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "FILE;SYMBOL;LIBRARY;HEADER" "USES;ROOTS")
    set(script "${PROJECT_SOURCE_DIR}/cmake/ChronoBatchRenderEmbed.cmake")
    if(ARG_FILE)
        set(source "${CMAKE_CURRENT_BINARY_DIR}/${ARG_SYMBOL}.cpp")
        set(inputs "${ARG_FILE}")
        set(arguments
            "-DINPUT=${ARG_FILE}"
            "-DSYMBOL=${ARG_SYMBOL}"
        )
    else()
        set(source "${CMAKE_CURRENT_BINARY_DIR}/${ARG_LIBRARY}.cpp")
        set(inputs "")
        foreach(root IN LISTS ARG_ROOTS)
            string(
                REPLACE "|"
                ";"
                parts
                "${root}"
            )
            list(GET parts 0 directory)
            list(GET parts 2 pattern)
            file(GLOB_RECURSE found CONFIGURE_DEPENDS "${directory}/${pattern}")
            list(APPEND inputs ${found})
        endforeach()
        list(SORT inputs)
        string(
            REPLACE ";"
            "\n"
            listed
            "${inputs}"
        )
        set(listing "${CMAKE_CURRENT_BINARY_DIR}/${ARG_LIBRARY}.inputs")
        file(CONFIGURE OUTPUT "${listing}" CONTENT "${listed}\n")
        list(APPEND inputs "${listing}")
        string(
            REPLACE ";"
            ","
            roots
            "${ARG_ROOTS}"
        )
        string(
            REPLACE ";"
            ","
            uses
            "${ARG_USES}"
        )
        set(arguments
            "-DROOTS=${roots}"
            "-DLIBRARY=${ARG_LIBRARY}"
            "-DUSES=${uses}"
        )
        if(ARG_HEADER)
            set(header "${CHRONO_BATCH_RENDER_GENERATED_INCLUDE_DIR}/${ARG_HEADER}")
            list(APPEND arguments "-DHEADER=${header}")
        endif()
    endif()
    set(output
        "${source}"
        ${header}
    )
    add_custom_command(
        OUTPUT
            ${output}
        COMMAND
            "${CMAKE_COMMAND}" ${arguments} "-DOUTPUT=${source}" -P "${script}"
        DEPENDS
            ${inputs}
            "${script}"
        VERBATIM
    )
    target_sources(${target} PRIVATE ${output})
endfunction()
