// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <benchmark/benchmark.h>

#include "hornetlib/crypto/cpuinfo.h"
#include "hornetlib/crypto/hash.h"
#include "hornetlib/crypto/sha256.h"
#include "hornetlib/crypto/sha256_ni.h"

using namespace hornet::crypto;

// Test data
static std::vector<uint8_t> GenerateTestData(size_t size) {
  std::vector<uint8_t> data(size);
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<uint8_t>(i * 7 + 13);  // Deterministic but non-zero
  }
  return data;
}

// Benchmark scalar SHA256 (current implementation)
static void BM_SHA256_Scalar(benchmark::State& state) {
  auto data = GenerateTestData(state.range(0));
  for (auto _ : state) {
    auto hash = sha::ComputeSHA256(data);
    benchmark::DoNotOptimize(hash);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

// Benchmark SHA-NI SHA256
#if defined(HORNET_HAS_SHA_NI)
static void BM_SHA256_SHANI(benchmark::State& state) {
  if (!HasSHAExtensions()) {
    state.SkipWithError("SHA-NI not supported on this CPU");
    return;
  }

  auto data = GenerateTestData(state.range(0));
  for (auto _ : state) {
    auto hash = SHA256::Hash_SHANI(data);
    benchmark::DoNotOptimize(hash);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
#endif

// Benchmark double-SHA256 scalar
static void BM_DoubleSHA256_Scalar(benchmark::State& state) {
  auto data = GenerateTestData(state.range(0));
  for (auto _ : state) {
    auto hash = Hash256(data);
    benchmark::DoNotOptimize(hash);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

// Benchmark double-SHA256 with SHA-NI
#if defined(HORNET_HAS_SHA_NI)
static void BM_DoubleSHA256_SHANI(benchmark::State& state) {
  if (!HasSHAExtensions()) {
    state.SkipWithError("SHA-NI not supported on this CPU");
    return;
  }

  auto data = GenerateTestData(state.range(0));
  for (auto _ : state) {
    // Double SHA256: hash the hash
    auto hash1 = SHA256::Hash_SHANI(data);
    auto hash2 = SHA256::Hash_SHANI(hash1);
    benchmark::DoNotOptimize(hash2);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
#endif

// Benchmark batched double-SHA256 (current scalar implementation)
static void BM_DoubleSHA256Batch_Scalar(benchmark::State& state) {
  const int batch_size = state.range(0);
  const int input_size = state.range(1);

  std::vector<uint8_t> input_data(batch_size * input_size);
  std::vector<uint8_t> output_data(batch_size * 32);

  for (int i = 0; i < batch_size * input_size; ++i) {
    input_data[i] = static_cast<uint8_t>(i * 7 + 13);
  }

  for (auto _ : state) {
    Hash256Batch(input_data.data(), input_size, input_size,
                      batch_size, output_data.data(), 32);
    benchmark::DoNotOptimize(output_data.data());
  }
  state.SetItemsProcessed(state.iterations() * batch_size);
  state.SetBytesProcessed(state.iterations() * batch_size * input_size);
}

// Benchmark batched double-SHA256 using SHA-NI (simple loop, no interleaving)
#if defined(HORNET_HAS_SHA_NI)
static void BM_DoubleSHA256Batch_SHANI(benchmark::State& state) {
  if (!HasSHAExtensions()) {
    state.SkipWithError("SHA-NI not supported on this CPU");
    return;
  }

  const int batch_size = state.range(0);
  const int input_size = state.range(1);

  std::vector<uint8_t> input_data(batch_size * input_size);
  std::vector<bytes32_t> output_data(batch_size);

  for (int i = 0; i < batch_size * input_size; ++i) {
    input_data[i] = static_cast<uint8_t>(i * 7 + 13);
  }

  for (auto _ : state) {
    for (int i = 0; i < batch_size; ++i) {
      const uint8_t* input = input_data.data() + i * input_size;
      auto hash1 = SHA256::Hash_SHANI(std::span<const uint8_t>(input, input_size));
      output_data[i] = SHA256::Hash_SHANI(hash1);
    }
    benchmark::DoNotOptimize(output_data.data());
  }
  state.SetItemsProcessed(state.iterations() * batch_size);
  state.SetBytesProcessed(state.iterations() * batch_size * input_size);
}
#endif

// Register benchmarks for different input sizes
// Common sizes:
// - 32 bytes: hash
// - 64 bytes: merkle tree nodes
// - 80 bytes: block headers
// - 226 bytes: typical P2PKH transaction (1 input, 2 outputs)
// - 192 bytes: typical P2WPKH SegWit transaction
// - 250-500 bytes: common transaction sizes
// - 1KB-10KB: large transactions
BENCHMARK(BM_SHA256_Scalar)->Arg(32)->Arg(64)->Arg(80)->Arg(192)->Arg(226)->Arg(250)->Arg(500)->Arg(1024)->Arg(10240);
#if defined(HORNET_HAS_SHA_NI)
BENCHMARK(BM_SHA256_SHANI)->Arg(32)->Arg(64)->Arg(80)->Arg(192)->Arg(226)->Arg(250)->Arg(500)->Arg(1024)->Arg(10240);
#endif

BENCHMARK(BM_DoubleSHA256_Scalar)->Arg(32)->Arg(64)->Arg(80)->Arg(192)->Arg(226)->Arg(250)->Arg(500)->Arg(1024);
#if defined(HORNET_HAS_SHA_NI)
BENCHMARK(BM_DoubleSHA256_SHANI)->Arg(32)->Arg(64)->Arg(80)->Arg(192)->Arg(226)->Arg(250)->Arg(500)->Arg(1024);
#endif

// Batch benchmarks: Args(batch_size, input_size)
// Test both block headers (80 bytes) and typical transactions (226 bytes)
BENCHMARK(BM_DoubleSHA256Batch_Scalar)->Args({2, 80})->Args({4, 80})->Args({8, 80})
    ->Args({16, 80})->Args({32, 80})->Args({100, 80})
    ->Args({2, 64})->Args({4, 64})->Args({8, 64})->Args({16, 64})
    ->Args({2, 226})->Args({4, 226})->Args({8, 226})->Args({16, 226});

#if defined(HORNET_HAS_SHA_NI)
BENCHMARK(BM_DoubleSHA256Batch_SHANI)->Args({2, 80})->Args({4, 80})->Args({8, 80})
    ->Args({16, 80})->Args({32, 80})->Args({100, 80})
    ->Args({2, 64})->Args({4, 64})->Args({8, 64})->Args({16, 64})
    ->Args({2, 226})->Args({4, 226})->Args({8, 226})->Args({16, 226});
#endif

BENCHMARK_MAIN();
