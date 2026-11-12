# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.22)
project(ChronoBatchRenderConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(chrono_batch_render 0.1 REQUIRED CONFIG OPTIONAL_COMPONENTS vulkan)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE chrono_batch_render::chrono_batch_render)

enable_testing()
add_test(NAME adapters COMMAND consumer)
add_test(
    NAME round_trip
    COMMAND
        consumer round-trip
)

if(TARGET chrono_batch_render::vulkan)
    add_executable(consumer_vulkan vulkan.cpp)
    target_link_libraries(consumer_vulkan PRIVATE chrono_batch_render::vulkan)
    add_test(NAME vulkan COMMAND consumer_vulkan)
endif()
