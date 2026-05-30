add_executable(hornetlib_bench trivial_bench.cpp)
target_link_libraries(hornetlib_bench benchmark::benchmark)

add_executable(sha256_bench sha256_bench.cpp)
target_link_libraries(sha256_bench hornetlib benchmark::benchmark)
target_include_directories(sha256_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(script_validation_bench script_validation_bench.cpp)
target_link_libraries(script_validation_bench hornetlib benchmark::benchmark)
target_include_directories(script_validation_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(secp256k1_bench secp256k1_bench.cpp)
target_link_libraries(secp256k1_bench hornetlib benchmark::benchmark)
target_include_directories(secp256k1_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)