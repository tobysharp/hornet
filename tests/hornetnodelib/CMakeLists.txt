# Unit test binary using GoogleTest
add_executable(hornetnodelib_tests
   net/bitcoind_test.cpp
   net/connection_test.cpp
   net/peer_test.cpp
   net/peer_manager_test.cpp
   net/socket_test.cpp
   node/protocol_loop_test.cpp
   node/sync_manager_test.cpp
)

target_compile_features(hornetnodelib_tests PRIVATE cxx_std_20)
target_link_libraries(hornetnodelib_tests PRIVATE hornetlib hornetnodelib testutil gtest_main)
target_include_directories(hornetnodelib_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)

include(GoogleTest)
gtest_discover_tests(hornetnodelib_tests)

# Enable CTest
include(CTest)
enable_testing()
