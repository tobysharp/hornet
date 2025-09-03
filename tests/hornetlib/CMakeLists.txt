# Unit test binary using GoogleTest
add_executable(hornetlib_tests
   consensus/difficulty_adjustment_test.cpp
   consensus/merkle_test.cpp
   consensus/validate_block_test.cpp
   consensus/validate_transaction_test.cpp
   crypto/hash_test.cpp
   data/chain_tree_test.cpp
   data/hashed_tree_test.cpp
   data/header_timechain_test.cpp
   data/keyframe_sidecar_test.cpp
   data/sidecar_test.cpp
   encoding/reader_test.cpp
   encoding/writer_test.cpp
   message/version_test.cpp
   message/verack_test.cpp
   model/header_context_test.cpp
   protocol/block_test.cpp
   protocol/block_header_test.cpp
   protocol/compact_target_test.cpp
   protocol/dispatch_test.cpp
   protocol/factory_test.cpp
   protocol/framer_test.cpp
   protocol/genesis_test.cpp
   protocol/handshake_test.cpp
   protocol/parser_test.cpp
   protocol/script/parser_test.cpp
   protocol/script/runtime/push_ops_test.cpp
   protocol/script/script_demo_test.cpp
   protocol/script/script_processor_test.cpp
   protocol/script/script_view_test.cpp
   protocol/script/script_writer_test.cpp
   util/big_uint_test.cpp
   util/hex_test.cpp
   util/pointer_iterator_test.cpp
   util/thread_safe_queue_test.cpp
   util/notify_test.cpp
)

target_compile_features(hornetlib_tests PRIVATE cxx_std_20)
target_link_libraries(hornetlib_tests PRIVATE hornetlib gtest_main)
target_include_directories(hornetlib_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)

include(GoogleTest)
gtest_discover_tests(hornetlib_tests)
