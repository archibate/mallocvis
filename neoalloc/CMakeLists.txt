cmake_minimum_required(VERSION 3.12)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

project(mallocvis LANGUAGES CXX)

add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-Wall$<SEMICOLON>-Wextra$<SEMICOLON>-Werror=return-type$<SEMICOLON>>)

add_library(neoalloc SHARED neoalloc.cpp)
find_package(Threads REQUIRED)
target_link_libraries(neoalloc PRIVATE Threads::Threads)
target_include_directories(neoalloc PUBLIC .)

add_executable(main main.cpp BM_mt.cpp)
target_link_libraries(main PRIVATE neoalloc)

find_package(benchmark REQUIRED)
target_link_libraries(main PRIVATE benchmark::benchmark benchmark::benchmark_main)
find_package(TBB REQUIRED)
target_link_libraries(main PRIVATE TBB::tbb)
