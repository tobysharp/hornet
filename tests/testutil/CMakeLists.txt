# tests/testutil/CMakeLists.txt
add_library(testutil STATIC
  net/bitcoind_peer.cpp
)
target_include_directories(testutil PUBLIC ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
