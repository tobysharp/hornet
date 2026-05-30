// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/reduce.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {
namespace {

constexpr auto kGeneratorCompressedSEC1 =
    "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"_bytes;
constexpr std::size_t kCorpusSize = 256;
static_assert((kCorpusSize & (kCorpusSize - 1)) == 0);

struct VerifySignatureBenchCase {
  secp256k1::PublicKey public_key;
  secp256k1::Signature signature;
  std::array<uint8_t, 32> digest;
};

struct PointAddBenchCase {
  secp256k1::Point lhs;
  secp256k1::Point rhs;
};

struct PointMultiplyBenchCase {
  secp256k1::Wide scalar;
  secp256k1::Point point;
};

uint64_t XorShift64(uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

UIntW<256> RandomUInt256(uint64_t& state) {
  return UIntW<256>{std::array<uint64_t, 4>{XorShift64(state), XorShift64(state), XorShift64(state), XorShift64(state)}};
}

std::array<uint8_t, 32> ToBigEndianBytes(const UIntW<256>& value) {
  std::array<uint8_t, 32> bytes{};
  for (int i = 0; i < UIntW<256>::kWords; ++i) {
    const auto word = value.Words()[i];
    for (int j = 0; j < int(sizeof(UIntW<256>::Word)); ++j)
      bytes[bytes.size() - 1 - (i * sizeof(UIntW<256>::Word) + j)] = uint8_t(word >> (j << 3));
  }
  return bytes;
}

secp256k1::Mod_n RandomNonZeroScalar(uint64_t& state) {
  auto reduced = RandomUInt256(state).Modulo(constants::n);
  if (reduced == UIntW<256>::Zero()) reduced = UIntW<256>{1};
  return secp256k1::Mod_n{reduced};
}

std::vector<VerifySignatureBenchCase> MakeVerifySignatureBenchCorpus() {
  std::vector<VerifySignatureBenchCase> corpus;
  corpus.reserve(kCorpusSize);
  const auto public_key = secp256k1::PublicKeyFromSEC1(kGeneratorCompressedSEC1);
  Assert(public_key.has_value());

  const secp256k1::Mod_n private_key{1};
  const secp256k1::Mod_n nonce{1};
  const secp256k1::Point nonce_point = nonce.x * secp256k1::G;
  const secp256k1::Mod_n r{nonce_point.NormalizedX().x.Modulo(constants::n)};

  uint64_t state = 0x6c8e9cf570932bd5ull;
  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto digest = ToBigEndianBytes(RandomUInt256(state));
    const secp256k1::Mod_n z{secp256k1::Wide::FromBigEndianBytes(digest)};
    const secp256k1::Mod_n s = (z + r * private_key) / nonce;
    VerifySignatureBenchCase bench_case{*public_key, {r.x, s.x}, digest};
    Assert(secp256k1::VerifySignature(bench_case.public_key, bench_case.signature, bench_case.digest));
    corpus.push_back(std::move(bench_case));
  }
  return corpus;
}

std::vector<PointAddBenchCase> MakePointAddBenchCorpus() {
  std::vector<PointAddBenchCase> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x31a158f4f6de2b19ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto lhs_scalar = RandomNonZeroScalar(state);
    auto rhs_scalar = RandomNonZeroScalar(state);
    while (rhs_scalar == lhs_scalar) rhs_scalar = RandomNonZeroScalar(state);

    const secp256k1::Point lhs = lhs_scalar.x * secp256k1::G;
    const secp256k1::Point rhs = rhs_scalar.x * secp256k1::G;

    PointAddBenchCase bench_case{lhs, rhs};
    Assert(!bench_case.lhs.IsInfinity());
    Assert(!bench_case.rhs.IsInfinity());
    const secp256k1::Affine lhs_affine = bench_case.lhs;
    const secp256k1::Affine rhs_affine = bench_case.rhs;
    Assert(lhs_affine.x != rhs_affine.x || lhs_affine.y != -rhs_affine.y);
    corpus.push_back(std::move(bench_case));
  }

  return corpus;
}

std::vector<PointMultiplyBenchCase> MakePointMultiplyBenchCorpus() {
  std::vector<PointMultiplyBenchCase> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x54f0d3bdc7a24e81ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto scalar = RandomNonZeroScalar(state);
    const auto point_scalar = RandomNonZeroScalar(state);
    PointMultiplyBenchCase bench_case{scalar.x, point_scalar.x * secp256k1::G};
    Assert(!bench_case.point.IsInfinity());
    corpus.push_back(std::move(bench_case));
  }

  return corpus;
}

std::vector<std::pair<UIntW<256>, UIntW<256>>> MakeMultiplyModuloBenchCorpus() {
  std::vector<std::pair<UIntW<256>, UIntW<256>>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x0f4b2c1d9876a5e3ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    auto x = RandomUInt256(state).Modulo(constants::p);
    auto y = RandomUInt256(state).Modulo(constants::p);
    if (x == UIntW<256>::Zero()) x = UIntW<256>{1};
    if (y == UIntW<256>::Zero()) y = UIntW<256>{1};
    corpus.emplace_back(x, y);
  }

  return corpus;
}

std::vector<UIntW<256>> MakeInvertModuloBenchCorpus() {
  std::vector<UIntW<256>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x7a5b3c1de490f628ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    auto x = RandomUInt256(state).Modulo(constants::p);
    if (x == UIntW<256>::Zero()) x = UIntW<256>{1};
    corpus.push_back(x);
  }

  return corpus;
}

void SetOpsPerSecondCounter(benchmark::State& state) {
  state.SetItemsProcessed(state.iterations());
  state.counters["ops/s"] = benchmark::Counter(static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

static void BM_Secp256k1_VerifySignature(benchmark::State& state) {
  const auto corpus = MakeVerifySignatureBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& bench_case = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto public_key = bench_case.public_key;
    auto signature = bench_case.signature;
    auto digest = bench_case.digest;
    benchmark::DoNotOptimize(public_key);
    benchmark::DoNotOptimize(signature);
    benchmark::DoNotOptimize(digest);
    auto verified = secp256k1::VerifySignature(public_key, signature, digest);
    benchmark::DoNotOptimize(verified);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_Secp256k1_PointAdd(benchmark::State& state) {
  const auto corpus = MakePointAddBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& bench_case = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto lhs = bench_case.lhs;
    auto rhs = bench_case.rhs;
    benchmark::DoNotOptimize(lhs);
    benchmark::DoNotOptimize(rhs);
    auto sum = lhs + rhs;
    benchmark::DoNotOptimize(sum);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_Secp256k1_PointMultiply(benchmark::State& state) {
  const auto corpus = MakePointMultiplyBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& bench_case = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto scalar = bench_case.scalar;
    auto point = bench_case.point;
    benchmark::DoNotOptimize(scalar);
    benchmark::DoNotOptimize(point);
    auto product = scalar * point;
    benchmark::DoNotOptimize(product);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_MultiplyModuloM_256_Secp256k1P(benchmark::State& state) {
  const auto corpus = MakeMultiplyModuloBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& [input_x, input_y] = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input_x;
    auto y = input_y;
    benchmark::DoNotOptimize(x);
    benchmark::DoNotOptimize(y);
    auto product = detail::MultiplyModuloM<256, constants::p>(x, y);
    benchmark::DoNotOptimize(product);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_ReduceModuloP_256_Secp256k1P(benchmark::State& state) {
  const auto corpus = MakeMultiplyModuloBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& [input_x, input_y] = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input_x;
    auto y = input_y;
    benchmark::DoNotOptimize(x);
    benchmark::DoNotOptimize(y);
    auto product = ReduceModuloP(x, y);
    benchmark::DoNotOptimize(product);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_InvertModuloOdd_256_Secp256k1P(benchmark::State& state) {
  const auto corpus = MakeInvertModuloBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& input = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input;
    benchmark::DoNotOptimize(x);
    auto inverse = detail::InvertModuloOdd<256, constants::p>(x);
    benchmark::DoNotOptimize(inverse);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

BENCHMARK(BM_Secp256k1_VerifySignature);
BENCHMARK(BM_Secp256k1_PointAdd);
BENCHMARK(BM_Secp256k1_PointMultiply);
BENCHMARK(BM_MultiplyModuloM_256_Secp256k1P);
BENCHMARK(BM_ReduceModuloP_256_Secp256k1P);
BENCHMARK(BM_InvertModuloOdd_256_Secp256k1P);

}  // namespace
}  // namespace hornet::crypto::ecdsa

BENCHMARK_MAIN();