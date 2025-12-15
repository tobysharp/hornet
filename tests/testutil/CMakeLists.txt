# tests/testutil/CMakeLists.txt
add_library(testutil STATIC
  data_path.cpp
  net/bitcoind_peer.cpp
)
target_include_directories(testutil PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(testutil PRIVATE TEST_DATA_FOLDER="${TEST_DATA_DIR}")

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
