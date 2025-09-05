# Unit test binary using GoogleTest
add_executable(hornetnodelib_tests
   net/connection_test.cpp
   net/peer_test.cpp
   net/peer_manager_test.cpp
   net/socket_test.cpp
   net/tcp_notification_sink_test.cpp
   dispatch/protocol_loop_test.cpp
   sync/sync_manager_test.cpp
)

target_compile_features(hornetnodelib_tests PRIVATE cxx_std_20)
target_link_libraries(hornetnodelib_tests PRIVATE hornetlib hornetnodelib testutil gtest_main)
target_include_directories(hornetnodelib_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)

include(GoogleTest)
gtest_discover_tests(hornetnodelib_tests)
