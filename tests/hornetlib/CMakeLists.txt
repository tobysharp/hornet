# Unit test binary using GoogleTest
add_executable(hornetlib_tests
   consensus/difficulty_adjustment_test.cpp
   crypto/hash_test.cpp
   data/hashed_tree_test.cpp
   data/header_chain_test.cpp
   data/header_context_test.cpp
   data/header_timechain_test.cpp
   encoding/reader_test.cpp
   encoding/writer_test.cpp
   message/version_test.cpp
   message/verack_test.cpp
   net/bitcoind_test.cpp
   net/connection_test.cpp
   net/peer_test.cpp
   net/peer_manager_test.cpp
   net/socket_test.cpp
   node/protocol_loop_test.cpp
   node/sync_manager_test.cpp
   protocol/block_header_test.cpp
   protocol/compact_target_test.cpp
   protocol/dispatch_test.cpp
   protocol/factory_test.cpp
   protocol/framer_test.cpp
   protocol/genesis_test.cpp
   protocol/handshake_test.cpp
   protocol/parser_test.cpp
   util/big_uint_test.cpp
   util/pointer_iterator_test.cpp
   util/thread_safe_queue_test.cpp
)

target_compile_features(hornetlib_tests PRIVATE cxx_std_20)
target_link_libraries(hornetlib_tests PRIVATE hornetlib gtest_main)
target_include_directories(hornetlib_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(hornetlib_tests)

# Enable CTest
include(CTest)
enable_testing()
