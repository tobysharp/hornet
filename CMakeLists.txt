cmake_minimum_required(VERSION 3.16)
project(hornet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Use libc++ if compiling with Clang ---
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Clang detected: using libc++")
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

# --- Set compile options that apply to ALL build types ---
# (We put compiler-independent warnings here if possible, or use generator expressions)
add_compile_options(-Wall -Wextra -Wpedantic -Wredundant-move -Wself-move)

# --- Set flags SPECIFIC to each build type ---

# --- Per-configuration flags ---
# Only append if not already added (so users can override)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
endif()

add_subdirectory(extern/googletest)
target_compile_options(gtest PRIVATE -Wno-deprecated-declarations)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests")
add_subdirectory(extern/benchmark)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(bench)