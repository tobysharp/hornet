cmake_minimum_required(VERSION 3.16)
project(hornet_node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Set compile options that apply to ALL build types ---
# (We put compiler-independent warnings here if possible, or use generator expressions)
add_compile_options(-Wall -Wextra -Wpedantic -Wmove)

# --- Set flags SPECIFIC to each build type ---

# For Debug builds, add -g for symbols and -O0 to disable optimizations,
# which makes the debugger's behavior more predictable.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# For Release builds, add optimization flags and define NDEBUG to disable asserts.
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# --- Special flags ---
# The -fno-elide-constructors flag was a temporary workaround for a clang-14 bug.
# It's best not to keep it permanently. If you still need it when using clang-14,
# you can add it specifically to the debug flags like this:
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-elide-constructors")

# Header-only core library
add_library(hornet STATIC
   data/header_timechain.cpp
   net/bitcoind.cpp
   net/socket.cpp
   node/processor.cpp
   node/engine.cpp
)
target_include_directories(hornet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(hornet PRIVATE cxx_std_20)

# Main app (optional)
add_executable(btchornet main.cpp)
target_compile_features(btchornet PRIVATE cxx_std_20)
target_link_libraries(btchornet PRIVATE hornet)

# GoogleTest submodule
add_subdirectory(extern/googletest)
target_compile_options(gtest PRIVATE -Wno-deprecated-declarations)

# Unit test binary using GoogleTest
add_executable(hornet_tests
   crypto/hash_test.cpp
   encoding/reader_test.cpp
   encoding/writer_test.cpp
   message/version_test.cpp
   message/verack_test.cpp
   net/bitcoind_test.cpp
   net/connection_test.cpp
   net/peer_test.cpp
   net/peer_manager_test.cpp
   net/socket_test.cpp
   node/engine_test.cpp
   node/sync_manager_test.cpp
   protocol/block_header_test.cpp
   protocol/dispatch_test.cpp
   protocol/factory_test.cpp
   protocol/framer_test.cpp
   protocol/handshake_test.cpp
   protocol/parser_test.cpp
   util/big_uint_test.cpp
   util/pointer_iterator_test.cpp
)
target_compile_features(hornet_tests PRIVATE cxx_std_20)
target_link_libraries(hornet_tests PRIVATE hornet gtest_main)

include(GoogleTest)
gtest_discover_tests(hornet_tests)

# Enable CTest
include(CTest)
enable_testing()
