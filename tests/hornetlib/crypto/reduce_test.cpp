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
      EXPECT_EQ(ReduceModuloP(x, y), ReferenceReduceModuloP(x, y)) << "x=" << x << ", y=" << y;
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
      EXPECT_EQ(ReduceModuloP(x, y), ReferenceReduceModuloP(x, y)) << "x=" << x << ", y=" << y;
    }
  }
}

TEST(ReduceModuloPTest, MatchesReferenceForRandomizedInputs) {
  uint64_t state = 0x9e3779b97f4a7c15ull;

  for (int i = 0; i < 1000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    EXPECT_EQ(ReduceModuloP(x, y), ReferenceReduceModuloP(x, y)) << "iteration=" << i;
  }
}

TEST(ReduceModuloPTest, ResultIsAlwaysCanonicalForRandomizedInputs) {
  uint64_t state = 0xd1b54a32d192ed03ull;

  for (int i = 0; i < 1000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    const Uint256 reduced = ReduceModuloP(x, y);
    EXPECT_LT(reduced, secp256k1::p) << "iteration=" << i;
  }
}

// ---- IsJacobianXEqual: the projective x-coordinate comparison used by verify (Step 2) ----
// Tests whether t == r (mod n) for t = X / Z^2 (mod p), without inverting Z.

// Encodes the affine x-coordinate `ax` as a Jacobian (X, Z): X = ax * Z^2 (mod p), so that
// ax == X / Z^2 (mod p) -- mirroring how a real Jacobian point carries its affine x.
Uint256 JacobianXForAffineX(const Uint256& ax, const Uint256& z) {
  return ReduceModuloP(ax, ReduceModuloP(z.Squared()));
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

}  // namespace
}  // namespace hornet::crypto::ecdsa