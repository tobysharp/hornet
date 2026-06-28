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

# Optional comparative baseline: bitcoin-core/libsecp256k1, fetched + built from source at a pinned
# tag. Scoped entirely to this benchmark target -- root/src builds and the dependency surface are
# untouched. Configure with -DHORNET_BENCH_LIBSECP256K1=OFF to skip the fetch (e.g. offline builds).
option(HORNET_BENCH_LIBSECP256K1 "Build the libsecp256k1 comparison verify benchmark" ON)
if(HORNET_BENCH_LIBSECP256K1)
  include(FetchContent)
  # Library only: no tests/benchmarks/examples, and only the core (ECDSA verify lives in core).
  set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_RECOVERY OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_EXTRAKEYS OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "" FORCE)
  set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    secp256k1
    GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
    GIT_TAG        v0.7.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(secp256k1)
  target_compile_options(secp256k1 PRIVATE -w)  # third-party C; mute our C++-oriented warning set
  target_link_libraries(secp256k1_bench secp256k1)
  target_compile_definitions(secp256k1_bench PRIVATE HORNET_HAVE_LIBSECP256K1)
endif()