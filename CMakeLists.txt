cmake_minimum_required(VERSION 3.15)
project(hornet_node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only core library
add_library(hornet STATIC
   net/bitcoind.cpp
   net/socket.cpp
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
   encoding/message_reader_test.cpp
   encoding/message_writer_test.cpp
   protocol/message_dispatch_test.cpp
   protocol/message_factory_test.cpp
   protocol/message_framer_test.cpp
   protocol/message_parser_test.cpp
   messages/version_test.cpp
   net/bitcoind_test.cpp
)
target_link_libraries(hornet_tests PRIVATE hornet gtest_main)

include(GoogleTest)
gtest_discover_tests(hornet_tests)

# Enable CTest
include(CTest)
enable_testing()
