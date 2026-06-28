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
#include "hornetlib/crypto/glv.h"
#include "hornetlib/crypto/naf.h"
#include "hornetlib/crypto/reduce.h"
#include "hornetlib/crypto/scale.h"
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
  Curve::PublicKey public_key;
  Curve::Signature signature;
  std::array<uint8_t, 32> digest;
};

struct PointAddBenchCase {
  Curve::Point lhs;
  Curve::Point rhs;
};

struct PointMultiplyBenchCase {
  Curve::Wide scalar;
  Curve::Point point;
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

Curve::Mod_n RandomNonZeroScalar(uint64_t& state) {
  auto reduced = RandomUInt256(state).Modulo(secp256k1::n);
  if (reduced == UIntW<256>::Zero()) reduced = UIntW<256>{1};
  return Curve::Mod_n{reduced};
}

std::array<uint8_t, 65> EncodeUncompressedPublicKey(const Curve::Affine& point) {
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
    const Curve::Mod_n private_key = RandomNonZeroScalar(state);
    const Curve::Affine public_point = private_key.x * Curve::G;
    const auto public_key = Curve::PublicKeyFromSEC1(EncodeUncompressedPublicKey(public_point));
    BenchCheck(public_key.has_value(), "random public key failed validation");

    const Curve::Mod_n nonce = RandomNonZeroScalar(state);
    const Curve::Point nonce_point = nonce.x * Curve::G;
    const Curve::Mod_n r{nonce_point.NormalizedX().x.Modulo(secp256k1::n)};
    if (r.x == UIntW<256>::Zero()) continue;

    const auto digest = ToBigEndianBytes(RandomUInt256(state));
    const Curve::Mod_n z{Curve::Wide::FromBigEndianBytes(digest)};
    const Curve::Mod_n s = (z + r * private_key) / nonce;
    if (s.x == UIntW<256>::Zero()) continue;

    VerifySignatureBenchCase bench_case{*public_key, {r.x, s.x}, digest};
    BenchCheck(Curve::VerifySignature(bench_case.public_key, bench_case.signature, bench_case.digest),
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

    const Curve::Point lhs = lhs_scalar.x * Curve::G;
    const Curve::Point rhs = rhs_scalar.x * Curve::G;

    PointAddBenchCase bench_case{lhs, rhs};
    Assert(!bench_case.lhs.IsInfinity());
    Assert(!bench_case.rhs.IsInfinity());
    const Curve::Affine lhs_affine = bench_case.lhs;
    const Curve::Affine rhs_affine = bench_case.rhs;
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
    PointMultiplyBenchCase bench_case{scalar.x, point_scalar.x * Curve::G};
    Assert(!bench_case.point.IsInfinity());
    corpus.push_back(std::move(bench_case));
  }

  return corpus;
}

struct LinCombBenchCase {
  Curve::Wide u1;
  Curve::Affine P;
  Curve::Wide u2;
  Curve::Affine Q;
};

std::vector<LinCombBenchCase> MakeLinCombCorpus() {
  std::vector<LinCombBenchCase> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x123456789abcdef0ull;
  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto u1 = RandomNonZeroScalar(state);
    const auto u2 = RandomNonZeroScalar(state);
    const Curve::Affine P = RandomNonZeroScalar(state).x * Curve::G;
    const Curve::Affine Q = RandomNonZeroScalar(state).x * Curve::G;
    corpus.push_back({u1.x, P, u2.x, Q});
  }
  return corpus;
}

std::vector<std::pair<UIntW<256>, UIntW<256>>> MakeMultiplyModuloBenchCorpus() {
  std::vector<std::pair<UIntW<256>, UIntW<256>>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x0f4b2c1d9876a5e3ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    auto x = RandomUInt256(state).Modulo(secp256k1::p);
    auto y = RandomUInt256(state).Modulo(secp256k1::p);
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
    auto x = RandomUInt256(state).Modulo(secp256k1::p);
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

// Production verify: the default path, now wNAF over the fixed-base generator table (built once
// by the pre-timing correctness pass / corpus construction, outside the timed region).
static void BM_Secp256k1_VerifySignature(benchmark::State& state) {
  RunVerifySignatureBench(state, [](const Curve::PublicKey& pk, const Curve::Signature& sig,
                                    const std::array<uint8_t, 32>& digest) {
    return Curve::VerifySignature(pk, sig, digest);
  });
}

// Joint-NAF verify, kept for comparison now that wNAF is the default path.
static void BM_Secp256k1_VerifySignature_JointNAF(benchmark::State& state) {
  RunVerifySignatureBench(state, [](const Curve::PublicKey& pk, const Curve::Signature& sig,
                                    const std::array<uint8_t, 32>& digest) {
    return Curve::VerifySignatureWith(pk, sig, digest,
        [](const Curve::Wide& u1, const Curve::Wide& u2, const Curve::Affine& Q) {
          return LinearCombination(u1, Curve::G, u2, Q);
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
  std::vector<Curve::Affine> rhs_affine;  // normalize outside the timed loop
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
  constexpr int kGWidth = 12;
  std::array<Curve::Affine, (1 << (kGWidth - 1))> g_table;
  PrecomputeTableAffine(Curve::G, {g_table.data(), g_table.size()});
  const std::span<const Curve::Affine> g_span{g_table.data(), g_table.size()};

  const auto corpus = MakeLinCombCorpus();
  for (const auto& c : corpus) {
    const Curve::Affine reference = LinearCombination(c.u1, Curve::G, c.u2, c.Q);
    const Curve::Affine actual = LinearCombination_wNAF(c.u1, g_span, c.u2, c.Q);
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
    auto r = LinearCombination_wNAF(u1, g_span, u2, Q);
    benchmark::DoNotOptimize(r);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

// GLV mirrors the verify kernel (curve.h VerifySignature): split u1, u2 via the lambda endomorphism,
// then a 4-term Strauss over the fixed G/phi(G) affine tables plus a per-call Q/phi(Q) table. The
// fixed G-side tables are built once before the timed region (as the lazy generator-table build is in
// real verify); the two splits and the Q-side table build stay inside the timed region, exactly as the
// per-verify combiner runs them. A differential check against joint NAF gates correctness per entry.
// This is the lincomb-level peer of BM_LinComb_wNAF / BM_LinComb_JointNAF for the "adopt only where
// measured faster" comparison.
static void BM_LinComb_GLV(benchmark::State& state) {
  constexpr int kGWidth = 12;  // matches BM_LinComb_wNAF and the verify default (BuildGeneratorTable)
  std::vector<Curve::Affine> g_base(1u << (kGWidth - 1)), g_phi(1u << (kGWidth - 1));
  PrecomputeTableAffine(Curve::G, std::span{g_base});
  const Curve::Mod_p beta{secp256k1::beta};
  for (std::size_t i = 0; i < g_base.size(); ++i) g_phi[i] = {beta * g_base[i].x, g_base[i].y};  // phi(G) = (beta*x, y)
  const std::span<const Curve::Affine> g_base_span{g_base}, g_phi_span{g_phi};

  const auto glv = [&](const Curve::Wide& u1, const Curve::Wide& u2, const Curve::Affine& Q) {
    const GlvTerm<std::span<const Curve::Affine>> g_term{SplitLambda(u1), g_base_span, g_phi_span};
    return LinearCombination_GLV(g_term, MakeVariableGlvTerm(SplitLambda(u2), Q));
  };

  const auto corpus = MakeLinCombCorpus();
  for (const auto& c : corpus) {
    const Curve::Affine reference = LinearCombination(c.u1, Curve::G, c.u2, c.Q);
    const Curve::Affine actual = glv(c.u1, c.u2, c.Q);
    BenchCheck(reference.x == actual.x && reference.y == actual.y, "GLV result disagrees with joint NAF");
  }

  std::size_t index = 0;
  for (auto _ : state) {
    const auto& c = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto u1 = c.u1, u2 = c.u2;
    auto Q = c.Q;  // G is the fixed base, so only Q varies per case
    benchmark::DoNotOptimize(u1);
    benchmark::DoNotOptimize(u2);
    benchmark::DoNotOptimize(Q);
    auto r = glv(u1, u2, Q);
    benchmark::DoNotOptimize(r);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

// Isolates the GLV scalar decomposition (SplitLambda): k -> (k1, k2) with k == k1 + k2*lambda (mod n)
// and |k1|, |k2| < 2^128. This split overhead is folded into BM_LinComb_GLV and the verify path; bench
// it alone so the glv.h follow-ups (round_div multiply-shift, fast ReduceModuloN over the generic
// long division) can be measured directly. lambda here is a correctness oracle only -- the
// decomposition uses the lattice basis, not lambda.
static void BM_GLV_SplitLambda(benchmark::State& state) {
  const auto lambda = "5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72"_h256;
  const auto residue = [](const SignedScalar& s) {
    return s.negative ? secp256k1::n - s.magnitude : s.magnitude;  // canonical [0, n) representative
  };

  uint64_t gen = 0x9e3779b97f4a7c15ull;
  std::vector<UIntW<256>> corpus;
  corpus.reserve(kCorpusSize);
  for (std::size_t i = 0; i < kCorpusSize; ++i) corpus.push_back(RandomNonZeroScalar(gen).x);  // in [1, n)

  for (const auto& k : corpus) {
    const auto split = SplitLambda(k);
    const Curve::Mod_n reconstructed =
        Curve::Mod_n{residue(split.k1)} + Curve::Mod_n{residue(split.k2)} * Curve::Mod_n{lambda};
    BenchCheck(reconstructed.x == k, "SplitLambda reconstruction != k");
    BenchCheck(split.k1.magnitude.SignificantBits() <= 128 && split.k2.magnitude.SignificantBits() <= 128,
               "SplitLambda parts exceed half-width bound");
  }

  std::size_t index = 0;
  for (auto _ : state) {
    auto k = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(k);
    auto split = SplitLambda(k);
    benchmark::DoNotOptimize(split);
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
    auto product = detail::MultiplyModuloM<256, secp256k1::p>(x, y);
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
    auto squared = detail::SquaredModuloM<256, secp256k1::p>(x);
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
    auto product = detail::MultiplyModuloM<256, secp256k1::p>(x, x);
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
    auto inverse = detail::InvertModuloOdd<256, secp256k1::p>(x);
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
BENCHMARK(BM_Secp256k1_VerifySignature_JointNAF);
BENCHMARK(BM_Secp256k1_PointAdd);
BENCHMARK(BM_Secp256k1_PointAddMixed);
BENCHMARK(BM_Secp256k1_PointDouble);
BENCHMARK(BM_Secp256k1_PointMultiply);
BENCHMARK(BM_LinComb<LinearCombination>)->Name("BM_LinComb_JointNAF");
BENCHMARK(BM_LinComb<LinearCombination_NAF_Disjoint>)->Name("BM_LinComb_DisjointNAF");
BENCHMARK(BM_LinComb_wNAF);
BENCHMARK(BM_LinComb_GLV);
BENCHMARK(BM_GLV_SplitLambda);
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