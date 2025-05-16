cmake_minimum_required(VERSION 3.15)
project(hornet_node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only core library
add_library(hornet STATIC
   net/bitcoind.cpp
   net/socket.cpp
   node/processor
   node/message_loop.cpp
)
target_include_directories(hornet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Main app (optional)
add_executable(btchornet main.cpp)
target_link_libraries(btchornet PRIVATE hornet)

# GoogleTest submodule
add_subdirectory(extern/googletest)

# Unit test binary using GoogleTest
add_executable(hornet_tests
   crypto/hash_test.cpp
   encoding/reader_test.cpp
   encoding/writer_test.cpp
   protocol/dispatch_test.cpp
   protocol/factory_test.cpp
   protocol/framer_test.cpp
   protocol/handshake_test.cpp
   protocol/parser_test.cpp
   message/version_test.cpp
   net/bitcoind_test.cpp
   net/connection_test.cpp
   net/peer_test.cpp
   net/peer_manager_test.cpp
   net/socket_test.cpp
)
target_link_libraries(hornet_tests PRIVATE hornet gtest_main)

include(GoogleTest)
gtest_discover_tests(hornet_tests)

# Enable CTest
include(CTest)
enable_testing()
