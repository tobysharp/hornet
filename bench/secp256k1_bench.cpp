// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
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

constexpr std::size_t kCorpusSize = 256;
static_assert((kCorpusSize & (kCorpusSize - 1)) == 0);

// Hard correctness gate for corpus invariants. Unlike Assert (a no-op under NDEBUG), this
// fires in release builds so a wrong computation can never be timed as if it were correct.
void BenchCheck(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "secp256k1_bench: corpus invariant failed: %s\n", message);
  std::abort();
}

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

std::array<uint8_t, 65> EncodeUncompressedPublicKey(const secp256k1::Affine& point) {
  std::array<uint8_t, 65> bytes{};
  bytes[0] = 0x04;
  const auto x = ToBigEndianBytes(point.x.x);
  const auto y = ToBigEndianBytes(point.y.x);
  std::copy(x.begin(), x.end(), bytes.begin() + 1);
  std::copy(y.begin(), y.end(), bytes.begin() + 33);
  return bytes;
}

// Builds a corpus of genuine ECDSA signatures over random key pairs (not the degenerate
// public-key == G case): for each entry pick a private key d, form Q = d*G, sign a random
// digest with a fresh nonce, and keep only valid (r, s). Every case is verified here, outside
// any timed region, so the benchmark always times a computation known to be correct.
std::vector<VerifySignatureBenchCase> MakeVerifySignatureBenchCorpus() {
  std::vector<VerifySignatureBenchCase> corpus;
  corpus.reserve(kCorpusSize);

  uint64_t state = 0x6c8e9cf570932bd5ull;
  while (corpus.size() < kCorpusSize) {
    const secp256k1::Mod_n private_key = RandomNonZeroScalar(state);
    const secp256k1::Affine public_point = private_key.x * secp256k1::G;
    const auto public_key = secp256k1::PublicKeyFromSEC1(EncodeUncompressedPublicKey(public_point));
    BenchCheck(public_key.has_value(), "random public key failed validation");

    const secp256k1::Mod_n nonce = RandomNonZeroScalar(state);
    const secp256k1::Point nonce_point = nonce.x * secp256k1::G;
    const secp256k1::Mod_n r{nonce_point.NormalizedX().x.Modulo(constants::n)};
    if (r.x == UIntW<256>::Zero()) continue;

    const auto digest = ToBigEndianBytes(RandomUInt256(state));
    const secp256k1::Mod_n z{secp256k1::Wide::FromBigEndianBytes(digest)};
    const secp256k1::Mod_n s = (z + r * private_key) / nonce;
    if (s.x == UIntW<256>::Zero()) continue;

    VerifySignatureBenchCase bench_case{*public_key, {r.x, s.x}, digest};
    BenchCheck(secp256k1::VerifySignature(bench_case.public_key, bench_case.signature, bench_case.digest),
               "generated signature did not verify");
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

struct LinCombBenchCase {
  secp256k1::Wide u1;
  secp256k1::Affine P;
  secp256k1::Wide u2;
  secp256k1::Affine Q;
};

std::vector<LinCombBenchCase> MakeLinCombCorpus() {
  std::vector<LinCombBenchCase> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x123456789abcdef0ull;
  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto u1 = RandomNonZeroScalar(state);
    const auto u2 = RandomNonZeroScalar(state);
    const secp256k1::Affine P = RandomNonZeroScalar(state).x * secp256k1::G;
    const secp256k1::Affine Q = RandomNonZeroScalar(state).x * secp256k1::G;
    corpus.push_back({u1.x, P, u2.x, Q});
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

std::vector<UIntW<512>> MakeReduceModuloPCorpus() {
  std::vector<UIntW<512>> corpus;
  corpus.reserve(kCorpusSize);
  const auto factors = MakeMultiplyModuloBenchCorpus();

  for (const auto& [x, y] : factors)
    corpus.push_back(x.MultiplyWide(y));

  return corpus;
}

std::vector<UIntW<256>> MakeFieldModuloPCorpus() {
  std::vector<UIntW<256>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x4ea91cb8d6723f05ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    auto x = RandomUInt256(state).Modulo(constants::p);
    if (x == UIntW<256>::Zero()) x = UIntW<256>{1};
    corpus.push_back(x);
  }

  return corpus;
}

std::vector<UIntW<256>> MakeBigUintBenchCorpus() {
  std::vector<UIntW<256>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x2d4f68a1b9c3e507ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i)
    corpus.push_back(RandomUInt256(state));

  return corpus;
}

void SetOpsPerSecondCounter(benchmark::State& state) {
  state.SetItemsProcessed(state.iterations());
  state.counters["ops/s"] = benchmark::Counter(static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

// Drives the full verify path (s^-1, the u1*G + u2*Q linear combination, and R normalization)
// over the shared random-key corpus. `verify` selects the linear-combination strategy; every
// corpus signature is checked to verify before timing, so a wrong path can never be timed.
template <class Verify>
static void RunVerifySignatureBench(benchmark::State& state, Verify verify) {
  const auto corpus = MakeVerifySignatureBenchCorpus();
  for (const auto& c : corpus)
    BenchCheck(verify(c.public_key, c.signature, c.digest), "corpus signature failed to verify");

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
    auto verified = verify(public_key, signature, digest);
    benchmark::DoNotOptimize(verified);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

// Production verify: canonical joint-NAF linear combination.
static void BM_Secp256k1_VerifySignature(benchmark::State& state) {
  RunVerifySignatureBench(state, [](const secp256k1::PublicKey& pk, const secp256k1::Signature& sig,
                                    const std::array<uint8_t, 32>& digest) {
    return secp256k1::VerifySignature(pk, sig, digest);
  });
}

// Same verify path with the linear combination swapped for wNAF: a fixed wide G-table (w=10)
// built once outside the timed region, plus a per-call table for the variable key Q. Reported
// alongside the joint-NAF verify so the two strategies compare like-for-like end to end.
static void BM_Secp256k1_VerifySignature_wNAF(benchmark::State& state) {
  constexpr int kGWidth = 10;
  std::array<secp256k1::Affine, (1 << (kGWidth - 1))> g_table;
  PrecomputeTableAffine(secp256k1::G, {g_table.data(), g_table.size()});
  const std::span<const secp256k1::Affine> g_span{g_table.data(), g_table.size()};

  RunVerifySignatureBench(state, [&](const secp256k1::PublicKey& pk, const secp256k1::Signature& sig,
                                     const std::array<uint8_t, 32>& digest) {
    return secp256k1::VerifySignatureWith(pk, sig, digest,
        [&](const secp256k1::Wide& u1, const secp256k1::Wide& u2, const secp256k1::Affine& Q) {
          return LinearCombination_wNAF<256, constants::p, constants::a>(u1, g_span, u2, Q);
        });
  });
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

static void BM_Secp256k1_PointDouble(benchmark::State& state) {
  const auto corpus = MakePointAddBenchCorpus();
  std::size_t index = 0;
  for (auto _ : state) {
    auto lhs = corpus[index].lhs;
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(lhs);
    auto doubled = lhs.Double();
    benchmark::DoNotOptimize(doubled);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

static void BM_Secp256k1_PointAddMixed(benchmark::State& state) {
  const auto corpus = MakePointAddBenchCorpus();
  std::vector<secp256k1::Affine> rhs_affine;  // normalize outside the timed loop
  rhs_affine.reserve(corpus.size());
  for (const auto& c : corpus) rhs_affine.push_back(c.rhs);
  std::size_t index = 0;
  for (auto _ : state) {
    auto lhs = corpus[index].lhs;
    auto rhs = rhs_affine[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(lhs);
    benchmark::DoNotOptimize(rhs);
    auto sum = lhs + rhs;  // mixed: Jacobian + Affine
    benchmark::DoNotOptimize(sum);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

template <auto Fn>
static void BM_LinComb(benchmark::State& state) {
  const auto corpus = MakeLinCombCorpus();
  std::size_t index = 0;
  for (auto _ : state) {
    const auto& c = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto u1 = c.u1, u2 = c.u2;
    auto P = c.P, Q = c.Q;
    benchmark::DoNotOptimize(u1);
    benchmark::DoNotOptimize(u2);
    benchmark::DoNotOptimize(P);
    benchmark::DoNotOptimize(Q);
    auto r = Fn(u1, P, u2, Q);
    benchmark::DoNotOptimize(r);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

// wNAF mirrors the verify kernel: a fixed wide base (the generator) with a precomputed affine
// table, plus a variable base Q. The table is built once, before the timed region, and a
// differential check against the joint-NAF result gates correctness for every corpus entry.
static void BM_LinComb_wNAF(benchmark::State& state) {
  constexpr int kGWidth = 10;
  std::array<secp256k1::Affine, (1 << (kGWidth - 1))> g_table;
  PrecomputeTableAffine(secp256k1::G, {g_table.data(), g_table.size()});
  const std::span<const secp256k1::Affine> g_span{g_table.data(), g_table.size()};

  const auto corpus = MakeLinCombCorpus();
  for (const auto& c : corpus) {
    const secp256k1::Affine reference = LinearCombination<256, constants::p, constants::a>(c.u1, secp256k1::G, c.u2, c.Q);
    const secp256k1::Affine actual = LinearCombination_wNAF<256, constants::p, constants::a>(c.u1, g_span, c.u2, c.Q);
    BenchCheck(reference.x == actual.x && reference.y == actual.y, "wNAF result disagrees with joint NAF");
  }

  std::size_t index = 0;
  for (auto _ : state) {
    const auto& c = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto u1 = c.u1, u2 = c.u2;
    auto Q = c.Q;  // P is the fixed generator, so only Q varies per case
    benchmark::DoNotOptimize(u1);
    benchmark::DoNotOptimize(u2);
    benchmark::DoNotOptimize(Q);
    auto r = LinearCombination_wNAF<256, constants::p, constants::a>(u1, g_span, u2, Q);
    benchmark::DoNotOptimize(r);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

static void BM_MultiplyModP_256_Secp256k1P(benchmark::State& state) {
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
  const auto corpus = MakeReduceModuloPCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& input = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input;
    benchmark::DoNotOptimize(x);
    auto reduced = ReduceModuloP(x);
    benchmark::DoNotOptimize(reduced);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_SquareModP_256_Secp256k1P(benchmark::State& state) {
  const auto corpus = MakeFieldModuloPCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& input = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input;
    benchmark::DoNotOptimize(x);
    auto squared = detail::SquaredModuloM<256, constants::p>(x);
    benchmark::DoNotOptimize(squared);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_MultiplySelfModP_256_Secp256k1P(benchmark::State& state) {
  const auto corpus = MakeFieldModuloPCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& input = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input;
    benchmark::DoNotOptimize(x);
    auto product = detail::MultiplyModuloM<256, constants::p>(x, x);
    benchmark::DoNotOptimize(product);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_InvertModuloOdd_256_Secp256k1P(benchmark::State& state) {
  const auto corpus = MakeFieldModuloPCorpus();
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

static void BM_BigUint256_MultiplyWideSelf(benchmark::State& state) {
  const auto corpus = MakeBigUintBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& input = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input;
    benchmark::DoNotOptimize(x);
    auto wide = x.MultiplyWide(x);
    benchmark::DoNotOptimize(wide);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

static void BM_BigUint256_Squared(benchmark::State& state) {
  const auto corpus = MakeBigUintBenchCorpus();
  std::size_t index = 0;

  for (auto _ : state) {
    const auto& input = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto x = input;
    benchmark::DoNotOptimize(x);
    auto squared = x.Squared();
    benchmark::DoNotOptimize(squared);
    benchmark::ClobberMemory();
  }

  SetOpsPerSecondCounter(state);
}

BENCHMARK(BM_Secp256k1_VerifySignature);
BENCHMARK(BM_Secp256k1_VerifySignature_wNAF);
BENCHMARK(BM_Secp256k1_PointAdd);
BENCHMARK(BM_Secp256k1_PointAddMixed);
BENCHMARK(BM_Secp256k1_PointDouble);
BENCHMARK(BM_Secp256k1_PointMultiply);
BENCHMARK(BM_LinComb<LinearCombination<256, constants::p, constants::a>>)->Name("BM_LinComb_JointNAF");
BENCHMARK(BM_LinComb<LinearCombination_NAF_Disjoint<256, constants::p, constants::a>>)->Name("BM_LinComb_DisjointNAF");
BENCHMARK(BM_LinComb_wNAF);
BENCHMARK(BM_MultiplyModP_256_Secp256k1P);
BENCHMARK(BM_MultiplySelfModP_256_Secp256k1P);
BENCHMARK(BM_SquareModP_256_Secp256k1P);
BENCHMARK(BM_ReduceModuloP_256_Secp256k1P);
BENCHMARK(BM_InvertModuloOdd_256_Secp256k1P);
BENCHMARK(BM_BigUint256_MultiplyWideSelf);
BENCHMARK(BM_BigUint256_Squared);

}  // namespace
}  // namespace hornet::crypto::ecdsa

BENCHMARK_MAIN();