// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/reduce.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {
namespace {

Uint256 ReferenceReduceModuloP(const Uint256& x, const Uint256& y) {
  return x.MultiplyWide(y).Modulo(secp256k1::p);
}

uint64_t XorShift64(uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

Uint256 RandomUint256(uint64_t& state) {
  return Uint256{std::array<uint64_t, 4>{XorShift64(state), XorShift64(state), XorShift64(state), XorShift64(state)}};
}

UIntW<512> RandomUint512(uint64_t& state) {
  return UIntW<512>{std::array<uint64_t, 8>{XorShift64(state), XorShift64(state), XorShift64(state), XorShift64(state),
                                            XorShift64(state), XorShift64(state), XorShift64(state), XorShift64(state)}};
}

// Slow-path oracle: full long division of the 512-bit input by n.
Uint256 ReferenceReduceModuloN(const UIntW<512>& x) {
  return x.Modulo(secp256k1::n);
}

TEST(ReduceModuloPTest, MatchesReferenceForEdgeCases) {
  const std::array<Uint256, 11> values = {
      Uint256::Zero(),
      Uint256{1},
      Uint256{2},
      Uint256{977},
      Uint256{uint64_t{1} << 32},
      secp256k1::p - Uint256{2},
      secp256k1::p - Uint256{1},
      secp256k1::p,
      secp256k1::p + Uint256{1},
      "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"_h256,
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256,
  };

  for (const auto& x : values) {
    for (const auto& y : values) {
      EXPECT_EQ(ReduceModuloP(x * y), ReferenceReduceModuloP(x, y)) << "x=" << x << ", y=" << y;
    }
  }
}

TEST(ReduceModuloPTest, MatchesReferenceForValuesAroundModulusBoundary) {
  const std::array<Uint256, 7> near_modulus = {
      secp256k1::p - Uint256{3},
      secp256k1::p - Uint256{2},
      secp256k1::p - Uint256{1},
      secp256k1::p,
      secp256k1::p + Uint256{1},
      secp256k1::p + Uint256{2},
      secp256k1::p + Uint256{3},
  };

  for (const auto& x : near_modulus) {
    for (const auto& y : near_modulus) {
      EXPECT_EQ(ReduceModuloP(x * y), ReferenceReduceModuloP(x, y)) << "x=" << x << ", y=" << y;
    }
  }
}

TEST(ReduceModuloPTest, MatchesReferenceForRandomizedInputs) {
  uint64_t state = 0x9e3779b97f4a7c15ull;

  for (int i = 0; i < 1000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    EXPECT_EQ(ReduceModuloP(x * y), ReferenceReduceModuloP(x, y)) << "iteration=" << i;
  }
}

TEST(ReduceModuloPTest, ResultIsAlwaysCanonicalForRandomizedInputs) {
  uint64_t state = 0xd1b54a32d192ed03ull;

  for (int i = 0; i < 1000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    const Uint256 reduced = ReduceModuloP(x * y);
    EXPECT_LT(reduced, secp256k1::p) << "iteration=" << i;
  }
}

// The 320-bit reducer must be total over its container: t1 = a_1*2^256 + b_1 with a_1 a full 64
// bits, not just the <= 2^289 the 512-bit fold produces. The legacy `a_1 * 977u` word-multiply
// truncates once a_1*977 exceeds 64 bits (a_1 > ~2^54), silently corrupting the residue.
TEST(ReduceModuloPTest, Reduces320BitInputsAcrossTheFullDomain) {
  const auto reduce_and_check = [](const UIntW<320>& t1) {
    EXPECT_EQ(ReduceModuloP(t1), t1.Modulo(secp256k1::p)) << "t1=" << t1;
    EXPECT_LT(ReduceModuloP(t1), secp256k1::p) << "t1=" << t1;
  };
  const auto with_top_word = [](uint64_t a_1) {
    return UIntW<320>{std::array<uint64_t, 5>{0, 0, 0, 0, a_1}};
  };

  // Largest top word whose *977 still fits in 64 bits, and the first that does not.
  constexpr uint64_t kTruncationBoundary = ~0ull / 977u;

  reduce_and_check(with_top_word(1));                        // 2^256
  reduce_and_check(with_top_word(1ull << 33));               // 2^289: the 512-fold ceiling
  reduce_and_check(with_top_word(kTruncationBoundary));      // last safe value for the legacy path
  reduce_and_check(with_top_word(kTruncationBoundary + 1));  // first truncating value
  reduce_and_check(with_top_word(1ull << 63));               // 2^319: hand-checkable witness
  reduce_and_check(UIntW<320>::Maximum());                   // 2^320 - 1

  uint64_t state = 0xc0ac29b7c97c50ddull;
  for (int i = 0; i < 1000; ++i) {
    reduce_and_check(UIntW<320>{std::array<uint64_t, 5>{XorShift64(state), XorShift64(state), XorShift64(state),
                                                        XorShift64(state), XorShift64(state)}});
  }
}

// ---- IsJacobianXEqual: the projective x-coordinate comparison used by verify (Step 2) ----
// Tests whether t == r (mod n) for t = X / Z^2 (mod p), without inverting Z.

// Encodes the affine x-coordinate `ax` as a Jacobian (X, Z): X = ax * Z^2 (mod p), so that
// ax == X / Z^2 (mod p) -- mirroring how a real Jacobian point carries its affine x.
Uint256 JacobianXForAffineX(const Uint256& ax, const Uint256& z) {
  return ReduceModuloP(ax * ReduceModuloP(z.Squared()));
}

Uint256 RandomNonZeroFieldElement(uint64_t& state) {
  Uint256 z = RandomUint256(state).Modulo(secp256k1::p);
  if (z == Uint256::Zero()) z = Uint256{1};
  return z;
}

TEST(IsJacobianXEqualTest, HandVerifiableSmallCases) {
  // Z = 1: t = X, so the test is just X == r.
  EXPECT_TRUE(IsJacobianXEqual(Uint256{5}, Uint256{1}, Uint256{5}));
  EXPECT_FALSE(IsJacobianXEqual(Uint256{5}, Uint256{1}, Uint256{6}));
  // Z = 2: t = X / 4. With X = 20, t = 5, so r = 5 matches and r = 6 does not.
  EXPECT_TRUE(IsJacobianXEqual(Uint256{20}, Uint256{2}, Uint256{5}));
  EXPECT_FALSE(IsJacobianXEqual(Uint256{20}, Uint256{2}, Uint256{6}));
}

TEST(IsJacobianXEqualTest, MatchesAffineReferenceAcrossRandomRescalings) {
  // For a random affine x in [0, p) encoded with a random nonzero Z, the projective test must agree
  // with computing the affine x and reducing mod n -- and must be independent of the Z chosen.
  uint64_t state = 0x1234567890abcdefull;
  for (int i = 0; i < 2000; ++i) {
    const Uint256 ax = RandomUint256(state).Modulo(secp256k1::p);
    const Uint256 z = RandomNonZeroFieldElement(state);
    const Uint256 X = JacobianXForAffineX(ax, z);
    const Uint256 expected_r = ax.Modulo(secp256k1::n);

    EXPECT_TRUE(IsJacobianXEqual(X, z, expected_r)) << "i=" << i;

    const Uint256 other_r = RandomUint256(state).Modulo(secp256k1::n);
    EXPECT_EQ(IsJacobianXEqual(X, z, other_r), expected_r == other_r) << "i=" << i;
  }
}

TEST(IsJacobianXEqualTest, IsInvariantToJacobianRescaling) {
  // One affine x represented by many different Z values must give the same answer every time.
  uint64_t state = 0xfeedfacecafebeefull;
  const Uint256 ax = RandomUint256(state).Modulo(secp256k1::p);
  const Uint256 r = ax.Modulo(secp256k1::n);
  for (int i = 0; i < 1000; ++i) {
    const Uint256 z = RandomNonZeroFieldElement(state);
    EXPECT_TRUE(IsJacobianXEqual(JacobianXForAffineX(ax, z), z, r)) << "i=" << i;
  }
}

TEST(IsJacobianXEqualTest, HandlesModNWrapWhenXExceedsGroupOrder) {
  // The rare (~2^-128) branch random curve points never reach: an affine x with n <= t < p, where
  // t mod n = t - n. Constructed directly, since no random point lands in [n, p).
  uint64_t state = 0x0badc0dedeadbeefull;
  const Uint256 p_minus_n = secp256k1::p - secp256k1::n;
  for (int i = 0; i < 1000; ++i) {
    const Uint256 ax = secp256k1::n + RandomUint256(state).Modulo(p_minus_n);  // n <= ax < p
    const Uint256 r = ax - secp256k1::n;                                       // == ax mod n, and r < p - n
    const Uint256 z = RandomNonZeroFieldElement(state);
    const Uint256 X = JacobianXForAffineX(ax, z);

    // Acceptance must come from the wrap branch: r != ax, so r*Z^2 == X cannot be what matches.
    EXPECT_NE(r, ax);
    EXPECT_TRUE(IsJacobianXEqual(X, z, r)) << "i=" << i;

    const Uint256 other_r = RandomUint256(state).Modulo(secp256k1::n);
    EXPECT_EQ(IsJacobianXEqual(X, z, other_r), ax.Modulo(secp256k1::n) == other_r) << "i=" << i;
  }
}

TEST(IsJacobianXEqualTest, RejectsWrapWhenRPlusNExceedsFieldPrime) {
  // Guard test: when r >= p - n the wrap comparison must be skipped, even though X is built so that
  // (r + n) * Z^2 == X (mod p) would match. Dropping the `r < p - n` guard would both wrongly accept
  // here and overflow `r + n` in Uint256.
  const Uint256 p_minus_n = secp256k1::p - secp256k1::n;
  const Uint256 span = secp256k1::n - p_minus_n;  // size of the range [p - n, n)
  uint64_t state = 0xabcdef0123456789ull;
  auto check = [&](const Uint256& r) {
    const Uint256 ax = r - p_minus_n;  // = r + n - p, the mod-p image of r + n; lies in [0, n)
    const Uint256 z = RandomNonZeroFieldElement(state);
    EXPECT_FALSE(IsJacobianXEqual(JacobianXForAffineX(ax, z), z, r)) << "r=" << r;
  };
  check(p_minus_n);                  // exactly the threshold: r == p - n must already be excluded
  check(secp256k1::n - Uint256{1});  // largest possible r
  for (int i = 0; i < 1000; ++i) check(p_minus_n + RandomUint256(state).Modulo(span));
}

// ---- ReduceModuloN: fast reduction of a 512-bit value modulo the group order n (Step 4) ----

TEST(ReduceModuloNTest, ReducesPowerOfTwo256ToCn) {
  // The fold's core identity: 2^256 == c_n == 2^128 + d (mod n), with d = 0x4551231950B75FC4402DA1732FC9BEBF.
  const UIntW<512> two_256 = UIntW<512>{std::array<uint64_t, 8>{0, 0, 0, 0, 1, 0, 0, 0}};
  const Uint256 c_n = Uint256{std::array<uint64_t, 4>{0x402DA1732FC9BEBFull, 0x4551231950B75FC4ull, 1ull, 0ull}};
  EXPECT_EQ(ReduceModuloN(two_256), c_n);
  EXPECT_EQ(ReduceModuloN(two_256), ReferenceReduceModuloN(two_256));
}

TEST(ReduceModuloNTest, MatchesReferenceForEdgeCases) {
  const Uint256 n = secp256k1::n;
  const std::array<UIntW<512>, 10> values = {
      UIntW<512>::Zero(),
      Uint256{1}.ZeroExtend<512>(),
      n.ZeroExtend<512>(),                 // == 0 (mod n)
      (n - Uint256{1}).ZeroExtend<512>(),  // n - 1: the largest canonical residue
      (n + Uint256{1}).ZeroExtend<512>(),  // == 1 (mod n)
      Uint256::Maximum().ZeroExtend<512>(),  // 2^256 - 1
      UIntW<512>{std::array<uint64_t, 8>{0, 0, 0, 0, 1, 0, 0, 0}},  // 2^256
      n.Squared(),                         // n^2 (exercises the top of the input range)
      (n - Uint256{1}).Squared(),
      UIntW<512>::Maximum(),               // 2^512 - 1: forces the widest fold and a final subtraction
  };
  for (const auto& x : values) {
    EXPECT_EQ(ReduceModuloN(x), ReferenceReduceModuloN(x)) << "x=" << x;
    EXPECT_LT(ReduceModuloN(x), n) << "x=" << x;
  }
}

TEST(ReduceModuloNTest, MatchesReferenceForRandomProducts) {
  // The production workload: reduce the 512-bit product of two mod-n scalars.
  uint64_t state = 0x243f6a8885a308d3ull;
  for (int i = 0; i < 2000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    const UIntW<512> prod = x.MultiplyWide(y);
    EXPECT_EQ(ReduceModuloN(prod), ReferenceReduceModuloN(prod)) << "i=" << i;
  }
}

TEST(ReduceModuloNTest, MatchesReferenceForRandomFullWidthInputs) {
  uint64_t state = 0x13198a2e03707344ull;
  for (int i = 0; i < 2000; ++i) {
    const UIntW<512> x = RandomUint512(state);
    EXPECT_EQ(ReduceModuloN(x), ReferenceReduceModuloN(x)) << "i=" << i;
  }
}

TEST(ReduceModuloNTest, ResultIsAlwaysCanonicalForRandomizedInputs) {
  uint64_t state = 0xa4093822299f31d0ull;
  for (int i = 0; i < 2000; ++i) {
    EXPECT_LT(ReduceModuloN(RandomUint512(state)), secp256k1::n) << "i=" << i;
  }
}

TEST(ReduceModuloNTest, Reduces256BitInputsWithOneConditionalSubtract) {
  // The 256-bit overload: any x < 2^256 < 2n needs at most one subtract. Used by HashToInt to
  // reduce digests with integer value >= n (the ~2^-128 case a raw Mod_n construction asserts on).
  const Uint256 n = secp256k1::n;
  const std::array<Uint256, 6> values = {
      Uint256::Zero(), Uint256{1}, n - Uint256{1}, n, n + Uint256{1}, Uint256::Maximum(),
  };
  for (const auto& x : values) {
    EXPECT_EQ(ReduceModuloN(x), x.Modulo(n)) << "x=" << x;
    EXPECT_LT(ReduceModuloN(x), n) << "x=" << x;
  }
}

// ---- ReduceModulo dispatch: the modulus-specialized variable template used by Fp ----

TEST(ReduceModuloDispatchTest, SpecializationsMatchGenericModuloForBothModuli) {
  // The p and n specializations must agree with long division; a mis-routed specialization
  // (e.g. n dispatched to the p reducer) diverges immediately on random products.
  uint64_t state = 0x082efa98ec4e6c89ull;
  for (int i = 0; i < 200; ++i) {
    const auto prod = RandomUint256(state) * RandomUint256(state);
    EXPECT_EQ((ReduceModulo<256, secp256k1::p>(prod)), prod.Modulo(secp256k1::p)) << "i=" << i;
    EXPECT_EQ((ReduceModulo<256, secp256k1::n>(prod)), prod.Modulo(secp256k1::n)) << "i=" << i;
  }

  // 256-bit inputs route to the width-matching conditional-subtract overloads of both moduli.
  const std::array<Uint256, 5> edges = {
      secp256k1::n - Uint256{1}, secp256k1::n, secp256k1::p - Uint256{1}, secp256k1::p,
      Uint256::Maximum(),
  };
  for (const auto& x : edges) {
    EXPECT_EQ((ReduceModulo<256, secp256k1::p>(x)), x.Modulo(secp256k1::p)) << "x=" << x;
    EXPECT_EQ((ReduceModulo<256, secp256k1::n>(x)), x.Modulo(secp256k1::n)) << "x=" << x;
  }
}

}  // namespace
}  // namespace hornet::crypto::ecdsa