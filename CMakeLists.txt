cmake_minimum_required(VERSION 3.18)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)

project(mallocvis LANGUAGES CXX)

add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-Wall$<SEMICOLON>-Wextra$<SEMICOLON>-Werror=return-type$<SEMICOLON>-rdynamic>)
add_link_options($<$<CXX_COMPILER_ID:GNU,Clang>:-rdynamic>)

add_library(mallocvis SHARED malloc_hook.cpp plot_actions.cpp)

add_executable(example example.cpp)
target_link_libraries(example PRIVATE mallocvis)
add_executable(mt_example mt_example.cpp)
target_link_libraries(mt_example PRIVATE mallocvis)

find_package(tsl-robin-map)
if (tsl-robin-map_FOUND)
    target_link_libraries(example PRIVATE tsl::robin_map)
    target_compile_definitions(example PRIVATE -DHAS_TSL)
endif()

find_package(absl)
if (absl_FOUND)
    target_link_libraries(example PRIVATE absl::flat_hash_set absl::node_hash_set)
    target_compile_definitions(example PRIVATE -DHAS_ABSL)
endif()

find_package(OpenVDB)
if (OpenVDB_FOUND)
    add_executable(vdb_example vdb_example.cpp)
    target_link_libraries(vdb_example PRIVATE mallocvis)
    target_link_libraries(vdb_example PRIVATE OpenVDB::openvdb)
endif()

find_package(OpenGL)
find_package(glfw3)
find_package(glm)
if (OpenGL_FOUND AND glfw3_FOUND AND glm_FOUND)
    add_executable(visualizer visualize.cpp)
    target_link_libraries(visualizer PRIVATE OpenGL::GL)
    target_link_libraries(visualizer PRIVATE glfw)
    target_link_libraries(visualizer PRIVATE glm)
endif()
