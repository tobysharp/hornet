add_executable(hornetlib_bench trivial_bench.cpp)
target_link_libraries(hornetlib_bench benchmark::benchmark)

add_executable(sha256_bench sha256_bench.cpp)
target_link_libraries(sha256_bench hornetlib benchmark::benchmark)
target_include_directories(sha256_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)