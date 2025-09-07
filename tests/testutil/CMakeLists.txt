# tests/testutil/CMakeLists.txt
add_library(testutil STATIC
  net/bitcoind_peer.cpp
)
target_include_directories(testutil PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)

add_executable(testutil_tests
  net/bitcoind_peer_test.cpp
)

target_link_libraries(testutil_tests
  PRIVATE
    testutil
    hornetlib
    hornetnodelib
    gtest
    gtest_main
)
