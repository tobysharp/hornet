# Unit test binary using GoogleTest
add_executable(hornetlib_tests
   consensus/difficulty_adjustment_test.cpp
   consensus/merkle_test.cpp
   consensus/algebra_test.cpp
   consensus/spending_test_harness.cpp
   consensus/validate_sigop_costs_test.cpp
   consensus/validate_block_test.cpp
   consensus/validate_chain_harness.cpp
   consensus/validate_spending_block_test.cpp
   consensus/validate_spending_input_test.cpp
   consensus/validate_spending_transaction_test.cpp
   consensus/validate_transaction_test.cpp
   crypto/curve_test.cpp
   crypto/element_graph_test.cpp
   crypto/element_test.cpp
   crypto/hash_test.cpp
   crypto/fp_test.cpp
   crypto/point_magnitude_test.cpp
   crypto/reduce_test.cpp
   crypto/signature_test.cpp
   data/block_io_test.cpp
   data/chain_tree_test.cpp
   data/hashed_tree_test.cpp
   data/header_timechain_test.cpp
   data/keyframe_sidecar_test.cpp
   data/sidecar_test.cpp
   data/utxo/atomic_vector_test.cpp
   data/utxo/block_outputs_test.cpp
   data/utxo/database_test.cpp
   data/utxo/directory_test.cpp
   data/utxo/filter_test.cpp
   data/utxo/index_test.cpp
   data/utxo/joiner_test.cpp
   data/utxo/memory_age_test.cpp
   data/utxo/memory_run_test.cpp
   data/utxo/outputs_table_test.cpp
   data/utxo/single_writer_test.cpp
   data/utxo/spend_pipeline_test.cpp
   data/utxo/table_test.cpp
   data/utxo/tiled_vector_test.cpp
   data/priority_shared_mutex_test.cpp
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
   protocol/script/runtime/stack_call_test.cpp
   protocol/script/runtime/push_ops_test.cpp
   protocol/script/runtime/ops/bitwise_test.cpp
   protocol/script/runtime/ops/control_test.cpp
   protocol/script/runtime/ops/crypto_test.cpp
   protocol/script/runtime/ops/sig_test.cpp
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

if(HORNET_HAS_SHA_NI)
   target_sources(hornetlib_tests PRIVATE crypto/sha256_test.cpp)
endif()

target_compile_features(hornetlib_tests PRIVATE cxx_std_20)
target_link_libraries(hornetlib_tests PRIVATE hornetlib testutil gtest_main)
target_include_directories(hornetlib_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)

include(GoogleTest)
gtest_discover_tests(hornetlib_tests)

# Magnitude-checked build of the crypto test suites: every TU (including the util sources it
# needs) is compiled with HORNETLIB_CHECK_MAGNITUDES so the runtime magnitude validation is live.
# Deliberately does NOT link hornetlib: its TUs instantiate the same inline FieldElement code
# unchecked, and mixing the two layouts in one binary is an ODR violation.
add_executable(hornetlib_checked_tests
   crypto/checked_build_guard.cpp
   crypto/element_graph_test.cpp
   crypto/element_test.cpp
   crypto/point_magnitude_test.cpp
   crypto/curve_test.cpp
   ${PROJECT_SOURCE_DIR}/src/hornetlib/util/notify.cpp
)
target_compile_definitions(hornetlib_checked_tests PRIVATE HORNETLIB_CHECK_MAGNITUDES=1)
target_compile_features(hornetlib_checked_tests PRIVATE cxx_std_20)
target_link_libraries(hornetlib_checked_tests PRIVATE gtest_main)
target_include_directories(hornetlib_checked_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
gtest_discover_tests(hornetlib_checked_tests TEST_SUFFIX ".checked")
