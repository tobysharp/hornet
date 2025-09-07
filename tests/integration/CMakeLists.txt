# Unit test binary using GoogleTest
add_executable(integration_tests
   validation/merkle_root_validation_test.cpp
)

target_compile_features(integration_tests PRIVATE cxx_std_20)
target_link_libraries(integration_tests PRIVATE hornetlib hornetnodelib testutil gtest_main)
target_include_directories(integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)

include(GoogleTest)
gtest_discover_tests(integration_tests)
