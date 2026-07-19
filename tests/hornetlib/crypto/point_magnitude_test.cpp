// Copyright 2026 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

// Per-operation magnitude-contract tests for the real point.h formulas.
//
// Contract under test: every point operation must admit operands at the documented entry
// magnitudes (the closure-analysis bounds below) -- the largest magnitudes production ever feeds
// it. Each operation runs at those bounds with max-limb and random-limb inputs, and its outputs
// are compared coordinate-for-coordinate against the same formula evaluated on canonical
// (magnitude-1) representations of the same residues (a formula is a polynomial map, so the
// results must agree mod p; on-curve membership is irrelevant to the magnitude contract).
//
// An insufficient formula annotation fails in BOTH build flavors: the checked build throws at the
// exact Negate/admission site (naming the operation), and the unchecked build wraps limbs and
// diverges from the canonical-input result on the deterministic max-limb inputs.

#include <cstdint>
#include <random>
#include <tuple>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/secp256k1.h"

namespace hornet::crypto::ecdsa {
namespace {

using FE = FieldElement;
using JacF = JacobianPoint;
using AffF = AffinePoint;
using Array = FE::Array;

constexpr int kWords = FE::kWords;

// Documented entry magnitudes: the bounds every formula must admit (closure analysis).
// Jacobian coordinates as produced by the add/double formulas; affine as produced by the
// table-rescale (x) and negation (y) paths.
constexpr int kJX = 40, kJY = 19, kJZ = 8;
constexpr int kAX = 2, kAY = 3;

const uint64_t* P52() {
  static const Array p52 = [] {
    Array words;
    for (int i = 0; i < kWords; ++i)
      words[i] = (secp256k1::p >> (52 * i)).LowBits<64>() & ((uint64_t{1} << 52) - 1);
    return words;
  }();
  return p52.data();
}

FE RandomAt(std::mt19937_64& rng, int magnitude) {
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = rng() % (uint64_t(magnitude) << 52);
  words[kWords - 1] = rng() % (uint64_t(magnitude) << 48);
  return {words, magnitude};
}

FE MaxAt(int magnitude) {
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = (uint64_t(magnitude) << 52) - 1;
  words[kWords - 1] = (uint64_t(magnitude) << 48) - 1;
  return {words, magnitude};
}

// Value-preserving magnitude inflation of a canonical element: adds (m-1)*p in the limbs, so the
// residue is unchanged while every limb sits near its magnitude-m bound.
FE Inflate(const FE& canonical, int magnitude) {
  Array words;
  for (int i = 0; i < kWords; ++i)
    words[i] = canonical.Words()[i] + uint64_t(magnitude - 1) * P52()[i];
  return {words, magnitude};
}

// Canonical mirrors: semantically equal magnitude-1 operands for the reference evaluation.
FE Mirror(const FE& x) { return FE{x.Pack()}; }
JacF Mirror(const JacF& p) { return {Mirror(p.X), Mirror(p.Y), Mirror(p.Z)}; }
AffF Mirror(const AffF& a) { return {Mirror(a.x), Mirror(a.y)}; }

void ExpectSameJacobian(const JacF& f, const JacF& p, const char* what) {
  EXPECT_EQ(f.X.Pack(), p.X.Pack()) << what << " X";
  EXPECT_EQ(f.Y.Pack(), p.Y.Pack()) << what << " Y";
  EXPECT_EQ(f.Z.Pack(), p.Z.Pack()) << what << " Z";
}

void ExpectSameAffine(const AffF& f, const AffF& p, const char* what) {
  EXPECT_EQ(f.x.Pack(), p.x.Pack()) << what << " x";
  EXPECT_EQ(f.y.Pack(), p.y.Pack()) << what << " y";
}

JacF RandomJacobian(std::mt19937_64& rng) {
  return {RandomAt(rng, kJX), RandomAt(rng, kJY), RandomAt(rng, kJZ)};
}
JacF MaxJacobian() { return {MaxAt(kJX), MaxAt(kJY), MaxAt(kJZ)}; }
AffF RandomAffine(std::mt19937_64& rng) { return {RandomAt(rng, kAX), RandomAt(rng, kAY)}; }
AffF MaxAffine() { return {MaxAt(kAX), MaxAt(kAY)}; }

// ---- Jacobian formulas ---------------------------------------------------------------------------

TEST(PointOperationTest, JacobianDoubleAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90901};
  for (int trial = 0; trial < 25; ++trial) {
    const JacF p = RandomJacobian(rng);
    ExpectSameJacobian(p.Double(), Mirror(p).Double(), "Double");
  }
  const JacF max = MaxJacobian();
  ExpectSameJacobian(max.Double(), Mirror(max).Double(), "Double(max)");
}

TEST(PointOperationTest, JacobianAddGeneralBranchAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90902};
  for (int trial = 0; trial < 25; ++trial) {
    const JacF p = RandomJacobian(rng);
    const JacF q = RandomJacobian(rng);
    ExpectSameJacobian(p + q, Mirror(p) + Mirror(q), "Jac+Jac");
  }
  const JacF max = MaxJacobian();
  const JacF q = RandomJacobian(rng);
  ExpectSameJacobian(max + q, Mirror(max) + Mirror(q), "Jac+Jac(max,lhs)");
  ExpectSameJacobian(q + max, Mirror(q) + Mirror(max), "Jac+Jac(max,rhs)");
}

TEST(PointOperationTest, JacobianAddDoublingBranchAdmitsEntryMagnitudes) {
  // H == 0 and r == 0: identical operands take the doubling branch at full entry magnitudes.
  std::mt19937_64 rng{90903};
  for (int trial = 0; trial < 25; ++trial) {
    const JacF p = RandomJacobian(rng);
    ExpectSameJacobian(p + p, Mirror(p) + Mirror(p), "Jac+Jac doubling");
  }
  const JacF max = MaxJacobian();
  ExpectSameJacobian(max + max, Mirror(max) + Mirror(max), "Jac+Jac doubling(max)");
}

TEST(PointOperationTest, JacobianUnaryMinusAndSubtractAdmitEntryMagnitudes) {
  // The subtract operators route through unary minus, and production feeds them fresh formula
  // outputs (joint-NAF precomputes -(P+Q) with Y at its full output magnitude), so negation must
  // admit the Y entry bound.
  std::mt19937_64 rng{90904};
  for (int trial = 0; trial < 25; ++trial) {
    const JacF p = RandomJacobian(rng);
    const JacF q = RandomJacobian(rng);
    ExpectSameJacobian(-p, -Mirror(p), "-Jac");
    ExpectSameJacobian(p - q, Mirror(p) - Mirror(q), "Jac-Jac");
  }
  const JacF max = MaxJacobian();
  ExpectSameJacobian(-max, -Mirror(max), "-Jac(max)");
}

// ---- Mixed formulas ------------------------------------------------------------------------------

TEST(PointOperationTest, MixedAddGeneralBranchAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90905};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF a = RandomAffine(rng);
    const JacF p = RandomJacobian(rng);
    ExpectSameJacobian(a + p, Mirror(a) + Mirror(p), "Aff+Jac");
    ExpectSameJacobian(p + a, Mirror(p) + Mirror(a), "Jac+Aff");
  }
  const AffF amax = MaxAffine();
  const JacF pmax = MaxJacobian();
  ExpectSameJacobian(amax + pmax, Mirror(amax) + Mirror(pmax), "Aff+Jac(max)");
}

TEST(PointOperationTest, MixedSubtractAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90906};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF a = RandomAffine(rng);
    const JacF p = RandomJacobian(rng);
    ExpectSameJacobian(a - p, Mirror(a) - Mirror(p), "Aff-Jac");
    ExpectSameJacobian(p - a, Mirror(p) - Mirror(a), "Jac-Aff");
  }
}

TEST(PointOperationTest, AddWithZRatioAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90907};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF a = RandomAffine(rng);
    const JacF p = RandomJacobian(rng);
    const auto [rf, ratio_f] = p.AddWithZRatio(a);
    const auto [rp, ratio_p] = Mirror(p).AddWithZRatio(Mirror(a));
    ExpectSameJacobian(rf, rp, "AddWithZRatio");
    EXPECT_EQ(ratio_f.Pack(), ratio_p.Pack()) << "AddWithZRatio ratio";
  }
}

TEST(PointOperationTest, MixedAddDoublingBranchAdmitsEntryMagnitudes) {
  // Same point on both sides, in different representations: Z holds a value-1 residue inflated to
  // the entry magnitude, so H and r are nonzero-limb representations of zero -- the semantic
  // branch predicates must still route to the doubling branch, at full magnitudes.
  std::mt19937_64 rng{90908};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF a = RandomAffine(rng);
    const JacF same{FE{a.x.Pack()}, FE{a.y.Pack()}, Inflate(FE{uint64_t{1}}, kJZ)};
    ExpectSameJacobian(a + same, Mirror(a) + Mirror(same), "Aff+Jac doubling");
  }
}

// ---- Affine formulas -----------------------------------------------------------------------------

TEST(PointOperationTest, AffineAddSubtractAndDoubleAdmitEntryMagnitudes) {
  std::mt19937_64 rng{90909};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF a = RandomAffine(rng);
    const AffF b = RandomAffine(rng);
    ExpectSameAffine(a + b, Mirror(a) + Mirror(b), "Aff+Aff");
    ExpectSameAffine(a - b, Mirror(a) - Mirror(b), "Aff-Aff");
    ExpectSameAffine(a.Double(), Mirror(a).Double(), "Aff Double");
  }
  const AffF max = MaxAffine();
  const AffF other = RandomAffine(rng);
  ExpectSameAffine(max + other, Mirror(max) + Mirror(other), "Aff+Aff(max)");
}

TEST(PointOperationTest, AffineResultsReenterEntryMagnitudes) {
  // Affine arithmetic chains through Scale, so its outputs must be storable back at the affine
  // entry bounds (the weak-normalize-on-return contract): limbs in weak range.
  std::mt19937_64 rng{90910};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF sum = RandomAffine(rng) + RandomAffine(rng);
    for (int i = 1; i < kWords - 1; ++i) {
      EXPECT_LT(sum.x.Words()[i], uint64_t(kAX) << 52) << "limb " << i;
      EXPECT_LT(sum.y.Words()[i], uint64_t(kAY) << 52) << "limb " << i;
    }
  }
}

TEST(PointOperationTest, AffineUnaryMinusAdmitsEntryMagnitudesAndRoundTrips) {
  std::mt19937_64 rng{90911};
  for (int trial = 0; trial < 25; ++trial) {
    const AffF a = RandomAffine(rng);
    ExpectSameAffine(-a, -Mirror(a), "-Aff");
    ExpectSameAffine(-(-a), Mirror(a), "-(-Aff)");
  }
}

// ---- Predicates and conversions at extreme representations ---------------------------------------

TEST(PointOperationTest, OnCurveAndInfinityHoldAtInflatedRepresentations) {
  // The generator, value-preserved but with every coordinate inflated to its entry magnitude:
  // semantic predicates must see through the representation.
  const AffF g_inflated{Inflate(FE{secp256k1::Gx}, kAX), Inflate(FE{secp256k1::Gy}, kAY)};
  EXPECT_TRUE(g_inflated.IsOnCurve());
  EXPECT_FALSE(g_inflated.IsInfinity());

  const JacF gj{Inflate(FE{secp256k1::Gx}, kJX), Inflate(FE{secp256k1::Gy}, kJY),
                Inflate(FE{uint64_t{1}}, kJZ)};
  EXPECT_TRUE(gj.IsOnCurve());
  EXPECT_FALSE(gj.IsInfinity());

  const JacF off{gj.X, Inflate(FE{secp256k1::Gy + UIntW<256>{1}}, kJY), gj.Z};
  EXPECT_FALSE(off.IsOnCurve());
}

TEST(PointOperationTest, ConversionToAffineAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90912};
  for (int trial = 0; trial < 10; ++trial) {
    const JacF p = RandomJacobian(rng);
    const AffF f = p;
    const AffF r = Mirror(p);
    ExpectSameAffine(f, r, "operator Affine");
    EXPECT_EQ(p.NormalizedX().Pack(), Mirror(p).NormalizedX().Pack()) << "NormalizedX";
  }
  const JacF max = MaxJacobian();
  const AffF f = max;
  const AffF r = Mirror(max);
  ExpectSameAffine(f, r, "operator Affine(max)");
}

TEST(PointOperationTest, IsJacobianXEqualAdmitsEntryMagnitudes) {
  std::mt19937_64 rng{90913};
  for (int trial = 0; trial < 25; ++trial) {
    const FE x = RandomAt(rng, kJX);
    const FE z = RandomAt(rng, kJZ);
    // The true normalized u = x / z^2 mod p, from canonical operands.
    const auto u = (Mirror(x) * Mirror(z).Inverse().Squared()).Pack();
    if (u < secp256k1::n) {
      EXPECT_TRUE(Curve::IsJacobianXEqual(x, z, u));
      EXPECT_FALSE(Curve::IsJacobianXEqual(x, z, (u == UIntW<256>{0} ? u + UIntW<256>{1} : u - UIntW<256>{1})));
    }
  }
}

}  // namespace
}  // namespace hornet::crypto::ecdsa
