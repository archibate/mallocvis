cmake_minimum_required(VERSION 3.18)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 20)

project(mallocvis LANGUAGES CXX)

add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-Wall$<SEMICOLON>-Wextra$<SEMICOLON>-Werror=return-type>)

add_library(mallocvis SHARED hook.cpp plot.cpp)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE mallocvis)

find_package(tsl-robin-map)
if (tsl-robin-map_FOUND)
    target_link_libraries(main PRIVATE tsl::robin_map)
    target_compile_definitions(main PRIVATE -DHAS_TSL)
endif()

find_package(absl)
if (absl_FOUND)
    target_link_libraries(main PRIVATE absl::flat_hash_set absl::node_hash_set)
    target_compile_definitions(main PRIVATE -DHAS_ABSL)
endif()

add_library(leakcheck SHARED check.cpp)
