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

#ifdef HORNET_HAVE_LIBSECP256K1
#include <secp256k1.h>  // bitcoin-core reference impl, fetched by bench/CMakeLists.txt for comparison
#endif

namespace hornet::crypto::ecdsa {
namespace {

// Both element representations under test: canonical 4x64 Fp and lazy-reduction 5x52 FieldElement.
// Suffixed _4x64/_5x52 benches instantiate both for comparison; unsuffixed benches run the
// production element (FieldElement), preserving name continuity with the perf history.
using Element4x64 = Fp<secp256k1::kBits, secp256k1::p>;
using Element5x52 = FieldElement;
using Curve4x64 = hornet::crypto::ecdsa::Curve<Element4x64>;
using Curve5x52 = hornet::crypto::ecdsa::Curve<Element5x52>;
using Curve = Curve5x52;
using Mod_n = Fp<secp256k1::kBits, secp256k1::n>;

constexpr std::size_t kCorpusSize = 256;
static_assert((kCorpusSize & (kCorpusSize - 1)) == 0);

// Hard correctness gate for corpus invariants. Unlike Assert (a no-op under NDEBUG), this
// fires in release builds so a wrong computation can never be timed as if it were correct.
void BenchCheck(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "secp256k1_bench: corpus invariant failed: %s\n", message);
  std::abort();
}

template <class C>
struct VerifySignatureBenchCase {
  typename C::PublicKey public_key;
  typename C::Signature signature;
  std::array<uint8_t, 32> digest;
};

template <class C>
struct PointAddBenchCase {
  typename C::Point lhs;
  typename C::Point rhs;
};

template <class C>
struct PointMultiplyBenchCase {
  Uint256 scalar;
  typename C::Point point;
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

Mod_n RandomNonZeroScalar(uint64_t& state) {
  auto reduced = RandomUInt256(state).Modulo(secp256k1::n);
  if (reduced == UIntW<256>::Zero()) reduced = UIntW<256>{1};
  return Mod_n{reduced};
}

template <class Element>
std::array<uint8_t, 65> EncodeUncompressedPublicKey(const AffinePoint<Element>& point) {
  std::array<uint8_t, 65> bytes{};
  bytes[0] = 0x04;
  const auto x = ToBigEndianBytes(point.x.Pack());
  const auto y = ToBigEndianBytes(point.y.Pack());
  std::copy(x.begin(), x.end(), bytes.begin() + 1);
  std::copy(y.begin(), y.end(), bytes.begin() + 33);
  return bytes;
}

// Builds a corpus of genuine ECDSA signatures over random key pairs (not the degenerate
// public-key == G case): for each entry pick a private key d, form Q = d*G, sign a random
// digest with a fresh nonce, and keep only valid (r, s). Every case is verified here, outside
// any timed region, so the benchmark always times a computation known to be correct.
template <class C>
std::vector<VerifySignatureBenchCase<C>> MakeVerifySignatureBenchCorpus() {
  std::vector<VerifySignatureBenchCase<C>> corpus;
  corpus.reserve(kCorpusSize);

  uint64_t state = 0x6c8e9cf570932bd5ull;
  while (corpus.size() < kCorpusSize) {
    const Mod_n private_key = RandomNonZeroScalar(state);
    const typename C::Affine public_point = private_key.x * C::G;
    const auto public_key = C::PublicKeyFromSEC1(EncodeUncompressedPublicKey(public_point));
    BenchCheck(public_key.has_value(), "random public key failed validation");

    const Mod_n nonce = RandomNonZeroScalar(state);
    const typename C::Point nonce_point = nonce.x * C::G;
    const Mod_n r{nonce_point.NormalizedX().Pack().Modulo(secp256k1::n)};
    if (r.x == UIntW<256>::Zero()) continue;

    const auto digest = ToBigEndianBytes(RandomUInt256(state));
    const Mod_n z{ReduceModuloN(UIntW<256>::FromBigEndianBytes(std::span<const uint8_t>{digest}))};
    const Mod_n s = (z + r * private_key) / nonce;
    if (s.x == UIntW<256>::Zero()) continue;

    VerifySignatureBenchCase<C> bench_case{*public_key, {r.x, s.x}, digest};
    BenchCheck(C::VerifySignature(bench_case.public_key, bench_case.signature, bench_case.digest),
               "generated signature did not verify");
    corpus.push_back(std::move(bench_case));
  }
  return corpus;
}

template <class C>
std::vector<PointAddBenchCase<C>> MakePointAddBenchCorpus() {
  std::vector<PointAddBenchCase<C>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x31a158f4f6de2b19ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto lhs_scalar = RandomNonZeroScalar(state);
    auto rhs_scalar = RandomNonZeroScalar(state);
    while (rhs_scalar == lhs_scalar) rhs_scalar = RandomNonZeroScalar(state);

    const typename C::Point lhs = lhs_scalar.x * C::G;
    const typename C::Point rhs = rhs_scalar.x * C::G;

    PointAddBenchCase<C> bench_case{lhs, rhs};
    Assert(!bench_case.lhs.IsInfinity());
    Assert(!bench_case.rhs.IsInfinity());
    const typename C::Affine lhs_affine = bench_case.lhs;
    const typename C::Affine rhs_affine = bench_case.rhs;
    Assert(lhs_affine.x != rhs_affine.x || lhs_affine.y != -rhs_affine.y);
    corpus.push_back(std::move(bench_case));
  }

  return corpus;
}

template <class C>
std::vector<PointMultiplyBenchCase<C>> MakePointMultiplyBenchCorpus() {
  std::vector<PointMultiplyBenchCase<C>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x54f0d3bdc7a24e81ull;

  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto scalar = RandomNonZeroScalar(state);
    const auto point_scalar = RandomNonZeroScalar(state);
    PointMultiplyBenchCase<C> bench_case{scalar.x, point_scalar.x * C::G};
    Assert(!bench_case.point.IsInfinity());
    corpus.push_back(std::move(bench_case));
  }

  return corpus;
}

template <class C>
struct LinCombBenchCase {
  Uint256 u1;
  typename C::Affine P;
  Uint256 u2;
  typename C::Affine Q;
};

template <class C>
std::vector<LinCombBenchCase<C>> MakeLinCombCorpus() {
  std::vector<LinCombBenchCase<C>> corpus;
  corpus.reserve(kCorpusSize);
  uint64_t state = 0x123456789abcdef0ull;
  for (std::size_t i = 0; i < kCorpusSize; ++i) {
    const auto u1 = RandomNonZeroScalar(state);
    const auto u2 = RandomNonZeroScalar(state);
    const typename C::Affine P = RandomNonZeroScalar(state).x * C::G;
    const typename C::Affine Q = RandomNonZeroScalar(state).x * C::G;
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
template <class C, class Verify>
static void RunVerifySignatureBench(benchmark::State& state, Verify verify) {
  const auto corpus = MakeVerifySignatureBenchCorpus<C>();
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

// The production verify per element type; each instantiation owns its static generator
// tables, built outside timing. Registered unsuffixed for the production element (perf-history
// name continuity) and _GLV_4x64 for the comparison instantiation.
template <class C>
static void BM_VerifySignature_GLV(benchmark::State& state) {
  C::BuildGeneratorTable();
  RunVerifySignatureBench<C>(state, [](const typename C::PublicKey& pk, const typename C::Signature& sig,
                                       const std::array<uint8_t, 32>& digest) {
    return C::VerifySignature(pk, sig, digest);
  });
}

// Joint-NAF verify, kept for comparison now that GLV is the default path.
static void BM_Secp256k1_VerifySignature_JointNAF(benchmark::State& state) {
  RunVerifySignatureBench<Curve>(state, [](const Curve::PublicKey& pk, const Curve::Signature& sig,
                                           const std::array<uint8_t, 32>& digest) {
    return Curve::VerifySignatureWith(pk, sig, digest,
        [](const Curve::Wide& u1, const Curve::Wide& u2, const Curve::Affine& Q) {
          return LinearCombination(u1, Curve::G, u2, Q);
        });
  });
}

// wNAF verify (separate fixed-G + variable-Q tables, no endomorphism), kept for comparison against
// the GLV default and joint NAF. Clean apples-to-apples: same VerifySignatureImpl (s^-1, the combine,
// IsJacobianXEqual), only the linear combination differs; the fixed G-table is precomputed once
// outside timing at the GLV default width, exactly as the real verify lazily builds it. Post-globalz
// the Q-side adds are mixed (PrecomputeTableGlobalZ in LinearCombination_wNAF), so this measures wNAF
// without the old jac+jac tax -- the re-timing the Step 3 plan calls for.
static void BM_Secp256k1_VerifySignature_wNAF(benchmark::State& state) {
  constexpr int kGWidth = 12;  // matches the GLV default generator-table width (curve.h)
  std::vector<Curve::Affine> g_table(1u << (kGWidth - 1));
  PrecomputeTableAffine(Curve::G, std::span{g_table});
  const std::span<const Curve::Affine> g_span{g_table};
  RunVerifySignatureBench<Curve>(state, [&](const Curve::PublicKey& pk, const Curve::Signature& sig,
                                            const std::array<uint8_t, 32>& digest) {
    return Curve::VerifySignatureWith(pk, sig, digest,
        [&](const Curve::Wide& u1, const Curve::Wide& u2, const Curve::Affine& Q) {
          return LinearCombination_wNAF(u1, g_span, u2, Q);
        });
  });
}

#ifdef HORNET_HAVE_LIBSECP256K1
// Comparative baseline: bitcoin-core/libsecp256k1's single-signature verify over the SAME corpus, so
// the hornet GLV/wNAF numbers can be read against the de-facto reference implementation. Parsing (the
// SEC1 public key and the compact (r, s)) and low-S normalization happen once outside the timed region,
// matching the hornet benches, which time only the verify math on already-parsed inputs. libsecp256k1
// rejects high-S signatures, so each is normalized to low-S here -- this changes only acceptance, not
// the verify arithmetic, keeping the comparison apples-to-apples. The context is created once outside
// timing; verify itself is context-light.
static void BM_Secp256k1_VerifySignature_Libsecp256k1(benchmark::State& state) {
  const auto corpus = MakeVerifySignatureBenchCorpus<Curve>();

  secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
  BenchCheck(ctx != nullptr, "libsecp256k1 context creation failed");

  struct ParsedCase {
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_signature signature;
    std::array<uint8_t, 32> digest;
  };
  std::vector<ParsedCase> parsed;
  parsed.reserve(corpus.size());
  for (const auto& c : corpus) {
    ParsedCase p{};
    const auto sec1 = EncodeUncompressedPublicKey(static_cast<const Curve::Affine&>(c.public_key));
    BenchCheck(secp256k1_ec_pubkey_parse(ctx, &p.pubkey, sec1.data(), sec1.size()) == 1,
               "libsecp256k1 failed to parse corpus public key");

    std::array<uint8_t, 64> compact{};
    const auto r = ToBigEndianBytes(c.signature.first);
    const auto s = ToBigEndianBytes(c.signature.second);
    std::copy(r.begin(), r.end(), compact.begin());
    std::copy(s.begin(), s.end(), compact.begin() + 32);
    BenchCheck(secp256k1_ecdsa_signature_parse_compact(ctx, &p.signature, compact.data()) == 1,
               "libsecp256k1 failed to parse corpus signature");
    secp256k1_ecdsa_signature_normalize(ctx, &p.signature, &p.signature);

    p.digest = c.digest;
    BenchCheck(secp256k1_ecdsa_verify(ctx, &p.signature, p.digest.data(), &p.pubkey) == 1,
               "libsecp256k1 failed to verify corpus signature");
    parsed.push_back(p);
  }

  std::size_t index = 0;
  for (auto _ : state) {
    const auto& pc = parsed[index];
    index = (index + 1) & (kCorpusSize - 1);
    auto signature = pc.signature;
    auto pubkey = pc.pubkey;
    auto digest = pc.digest;
    benchmark::DoNotOptimize(signature);
    benchmark::DoNotOptimize(pubkey);
    benchmark::DoNotOptimize(digest);
    auto verified = secp256k1_ecdsa_verify(ctx, &signature, digest.data(), &pubkey);
    benchmark::DoNotOptimize(verified);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);

  secp256k1_context_destroy(ctx);
}
#endif  // HORNET_HAVE_LIBSECP256K1

template <class C>
static void BM_PointAdd(benchmark::State& state) {
  const auto corpus = MakePointAddBenchCorpus<C>();
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

template <class C>
static void BM_PointMultiply(benchmark::State& state) {
  const auto corpus = MakePointMultiplyBenchCorpus<C>();
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

template <class C>
static void BM_PointDouble(benchmark::State& state) {
  const auto corpus = MakePointAddBenchCorpus<C>();
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

template <class C>
static void BM_PointAddMixed(benchmark::State& state) {
  const auto corpus = MakePointAddBenchCorpus<C>();
  std::vector<typename C::Affine> rhs_affine;  // normalize outside the timed loop
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

template <class C, auto Fn>
static void BM_LinComb(benchmark::State& state) {
  const auto corpus = MakeLinCombCorpus<C>();
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

template <class C>
static void BM_LinComb_JointNAF(benchmark::State& state) {
  BM_LinComb<C, LinearCombination<typename C::Mod_p>>(state);
}

template <class C>
static void BM_LinComb_DisjointNAF(benchmark::State& state) {
  BM_LinComb<C, LinearCombination_NAF_Disjoint<typename C::Mod_p>>(state);
}

// wNAF mirrors the verify kernel: a fixed wide base (the generator) with a precomputed affine
// table, plus a variable base Q. The table is built once, before the timed region, and a
// differential check against the joint-NAF result gates correctness for every corpus entry.
template <class C>
static void BM_LinComb_wNAF(benchmark::State& state) {
  constexpr int kGWidth = 12;
  std::vector<typename C::Affine> g_table(1u << (kGWidth - 1));
  PrecomputeTableAffine(C::G, std::span{g_table});
  const std::span<const typename C::Affine> g_span{g_table};

  const auto corpus = MakeLinCombCorpus<C>();
  for (const auto& c : corpus) {
    const typename C::Affine reference = LinearCombination(c.u1, C::G, c.u2, c.Q);
    const typename C::Affine actual = LinearCombination_wNAF(c.u1, g_span, c.u2, c.Q);
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
template <class C>
static void BM_LinComb_GLV(benchmark::State& state) {
  using Element = typename C::Mod_p;
  constexpr int kGWidth = 12;  // matches the BuildGeneratorTable default (curve.h)
  std::vector<typename C::Affine> g_base(1u << (kGWidth - 1)), g_phi(1u << (kGWidth - 1));
  PrecomputeTableAffine(C::G, std::span{g_base});
  MakePhiTable<typename C::Affine>(std::span{g_base}, std::span{g_phi});
  const std::span<const typename C::Affine> g_base_span{g_base}, g_phi_span{g_phi};

  const auto glv = [&](const Uint256& u1, const Uint256& u2, const typename C::Affine& Q) {
    const GlvTerm<std::span<const typename C::Affine>, Element> g_term{SplitLambda(u1), g_base_span, g_phi_span};
    return LinearCombination_GLV(g_term, MakeVariableGlvTerm(SplitLambda(u2), Q));
  };

  const auto corpus = MakeLinCombCorpus<C>();
  for (const auto& c : corpus) {
    const typename C::Affine reference = LinearCombination(c.u1, C::G, c.u2, c.Q);
    const typename C::Affine actual = glv(c.u1, c.u2, c.Q);
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
    return s.negative ? secp256k1::n - s.magnitude
                      : s.magnitude.ZeroExtend<256>();  // canonical [0, n) representative
  };

  uint64_t gen = 0x9e3779b97f4a7c15ull;
  std::vector<UIntW<256>> corpus;
  corpus.reserve(kCorpusSize);
  for (std::size_t i = 0; i < kCorpusSize; ++i) corpus.push_back(RandomNonZeroScalar(gen).x);  // in [1, n)

  for (const auto& k : corpus) {
    const auto split = SplitLambda(k);
    const Mod_n reconstructed = Mod_n{residue(split.k1)} + Mod_n{residue(split.k2)} * Mod_n{lambda};
    BenchCheck(reconstructed.x == k, "SplitLambda reconstruction != k");
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

// Element-level comparison benches: identical canonical inputs fed to both representations.
// FieldElement inputs are magnitude 1 (fresh Unpack per case), matching the common post-normalize
// state; its mul/add results are not re-normalized, exercising the lazy-reduction fast path.
template <class Element>
static void BM_FieldMul(benchmark::State& state) {
  const auto raw = MakeMultiplyModuloBenchCorpus();
  std::vector<std::pair<Element, Element>> corpus;
  corpus.reserve(raw.size());
  for (const auto& [x, y] : raw) corpus.emplace_back(Element{x}, Element{y});

  std::size_t index = 0;
  for (auto _ : state) {
    auto [x, y] = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(x);
    benchmark::DoNotOptimize(y);
    auto product = x * y;
    benchmark::DoNotOptimize(product);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

template <class Element>
static void BM_FieldSquare(benchmark::State& state) {
  const auto raw = MakeFieldModuloPCorpus();
  std::vector<Element> corpus;
  corpus.reserve(raw.size());
  for (const auto& x : raw) corpus.emplace_back(x);

  std::size_t index = 0;
  for (auto _ : state) {
    auto x = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(x);
    auto squared = x.Squared();
    benchmark::DoNotOptimize(squared);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

template <class Element>
static void BM_FieldAdd(benchmark::State& state) {
  const auto raw = MakeMultiplyModuloBenchCorpus();
  std::vector<std::pair<Element, Element>> corpus;
  corpus.reserve(raw.size());
  for (const auto& [x, y] : raw) corpus.emplace_back(Element{x}, Element{y});

  std::size_t index = 0;
  for (auto _ : state) {
    auto [x, y] = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(x);
    benchmark::DoNotOptimize(y);
    auto sum = x + y;
    benchmark::DoNotOptimize(sum);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

template <class Element>
static void BM_FieldInverse(benchmark::State& state) {
  const auto raw = MakeFieldModuloPCorpus();
  std::vector<Element> corpus;
  corpus.reserve(raw.size());
  for (const auto& x : raw) corpus.emplace_back(x);

  std::size_t index = 0;
  for (auto _ : state) {
    auto x = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(x);
    auto inverse = x.Inverse();
    benchmark::DoNotOptimize(inverse);
    benchmark::ClobberMemory();
  }
  SetOpsPerSecondCounter(state);
}

// 5x52-only: the deferred-reduction costs Fp pays inline. Inputs are magnitude-2 mul outputs,
// the typical pre-normalize state in the point formulas.
template <auto Normalize>
static void BM_FieldNormalize5x52(benchmark::State& state) {
  const auto raw = MakeMultiplyModuloBenchCorpus();
  std::vector<Element5x52> corpus;
  corpus.reserve(raw.size());
  for (const auto& [x, y] : raw) corpus.push_back(Element5x52{x} * Element5x52{y});

  std::size_t index = 0;
  for (auto _ : state) {
    auto x = corpus[index];
    index = (index + 1) & (kCorpusSize - 1);
    benchmark::DoNotOptimize(x);
    auto normalized = (x.*Normalize)();
    benchmark::DoNotOptimize(normalized);
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
    auto product = ReduceModulo<256, secp256k1::p>(x * y);
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
    auto squared = ReduceModulo<256, secp256k1::p>(x.Squared());
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
    auto product = ReduceModulo<256, secp256k1::p>(x * x);
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

BENCHMARK(BM_VerifySignature_GLV<Curve5x52>)->Name("BM_Secp256k1_VerifySignature");
BENCHMARK(BM_VerifySignature_GLV<Curve4x64>)->Name("BM_Secp256k1_VerifySignature_GLV_4x64");
BENCHMARK(BM_Secp256k1_VerifySignature_JointNAF);
BENCHMARK(BM_Secp256k1_VerifySignature_wNAF);
#ifdef HORNET_HAVE_LIBSECP256K1
BENCHMARK(BM_Secp256k1_VerifySignature_Libsecp256k1);
#endif
BENCHMARK(BM_PointAdd<Curve4x64>)->Name("BM_Secp256k1_PointAdd_4x64");
BENCHMARK(BM_PointAdd<Curve5x52>)->Name("BM_Secp256k1_PointAdd_5x52");
BENCHMARK(BM_PointAddMixed<Curve4x64>)->Name("BM_Secp256k1_PointAddMixed_4x64");
BENCHMARK(BM_PointAddMixed<Curve5x52>)->Name("BM_Secp256k1_PointAddMixed_5x52");
BENCHMARK(BM_PointDouble<Curve4x64>)->Name("BM_Secp256k1_PointDouble_4x64");
BENCHMARK(BM_PointDouble<Curve5x52>)->Name("BM_Secp256k1_PointDouble_5x52");
BENCHMARK(BM_PointMultiply<Curve4x64>)->Name("BM_Secp256k1_PointMultiply_4x64");
BENCHMARK(BM_PointMultiply<Curve5x52>)->Name("BM_Secp256k1_PointMultiply_5x52");
BENCHMARK(BM_LinComb_JointNAF<Curve4x64>)->Name("BM_LinComb_JointNAF_4x64");
BENCHMARK(BM_LinComb_JointNAF<Curve5x52>)->Name("BM_LinComb_JointNAF_5x52");
BENCHMARK(BM_LinComb_DisjointNAF<Curve4x64>)->Name("BM_LinComb_DisjointNAF_4x64");
BENCHMARK(BM_LinComb_DisjointNAF<Curve5x52>)->Name("BM_LinComb_DisjointNAF_5x52");
BENCHMARK(BM_LinComb_wNAF<Curve4x64>)->Name("BM_LinComb_wNAF_4x64");
BENCHMARK(BM_LinComb_wNAF<Curve5x52>)->Name("BM_LinComb_wNAF_5x52");
BENCHMARK(BM_LinComb_GLV<Curve4x64>)->Name("BM_LinComb_GLV_4x64");
BENCHMARK(BM_LinComb_GLV<Curve5x52>)->Name("BM_LinComb_GLV_5x52");
BENCHMARK(BM_GLV_SplitLambda);
BENCHMARK(BM_FieldMul<Element4x64>)->Name("BM_Field_Mul_4x64");
BENCHMARK(BM_FieldMul<Element5x52>)->Name("BM_Field_Mul_5x52");
BENCHMARK(BM_FieldSquare<Element4x64>)->Name("BM_Field_Square_4x64");
BENCHMARK(BM_FieldSquare<Element5x52>)->Name("BM_Field_Square_5x52");
BENCHMARK(BM_FieldAdd<Element4x64>)->Name("BM_Field_Add_4x64");
BENCHMARK(BM_FieldAdd<Element5x52>)->Name("BM_Field_Add_5x52");
BENCHMARK(BM_FieldInverse<Element4x64>)->Name("BM_Field_Inverse_4x64");
BENCHMARK(BM_FieldInverse<Element5x52>)->Name("BM_Field_Inverse_5x52");
BENCHMARK(BM_FieldNormalize5x52<&Element5x52::NormalizeWeak>)->Name("BM_Field_NormalizeWeak_5x52");
BENCHMARK(BM_FieldNormalize5x52<&Element5x52::Normalize>)->Name("BM_Field_Normalize_5x52");
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
