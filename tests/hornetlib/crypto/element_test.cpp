// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <random>
#include <type_traits>

#include <gtest/gtest.h>

#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/fp.h"

namespace hornet::crypto::ecdsa {
namespace {

template <size_t kBits, std::unsigned_integral T>
void PrintTo(const util::BigUint<kBits, T>& value, std::ostream* os) {
  *os << "BigUint<" << kBits << ", " << sizeof(T) * 8 << ">{";
  for (int index = util::BigUint<kBits, T>::kWords - 1; index >= 0; --index) {
    *os << std::hex << std::setfill('0') << std::setw(sizeof(T) * 2) << value.Words()[index];
    if (index > 0) *os << "_";
  }
  *os << std::dec << "}";
}

using Element = FieldElement<1>;
using Array = Element::Array;

constexpr int kWords = Element::kWords;
// Invariant under test: for magnitude m, words[0..3] < m * 2^52 and words[4] < m * 2^48.
constexpr uint64_t kLowBound = uint64_t{1} << 52;
constexpr uint64_t kTopBound = uint64_t{1} << 48;

// The integer represented by the limb vector: sum of words[i] * 2^(52*i). Limbwise addition is
// exact integer addition (no reduction), so this is the oracle for all addition tests.
template <int kMagnitude>
UIntW<320> ToInteger(const FieldElement<kMagnitude>& x) {
  UIntW<320> sum = 0;
  for (int i = 0; i < kWords; ++i) sum = sum + (UIntW<320>{x.Words()[i]} << (52 * i));
  return sum;
}

template <int kMagnitude>
FieldElement<kMagnitude> RandomElement(std::mt19937_64& rng) {
  // 4096 * 2^52 wraps; anything below keeps the modulus arithmetic overflow-free.
  static_assert(kMagnitude >= 1 && kMagnitude <= (1 << 12) - 1);
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = rng() % (kMagnitude * kLowBound);
  words[kWords - 1] = rng() % (kMagnitude * kTopBound);
  return FieldElement<kMagnitude>{words};
}

TEST(FieldElementTest, DefaultConstructorIsZero) {
  constexpr Element zero{};
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(zero.Words()[i], 0u);
}

TEST(FieldElementTest, WordConstructorSetsLowWord) {
  const Element x{0xABCDEFull};
  EXPECT_EQ(x.Words()[0], 0xABCDEFull);
  for (int i = 1; i < kWords; ++i) EXPECT_EQ(x.Words()[i], 0u);

  // The largest single word legal at magnitude 1.
  const Element max{kLowBound - 1};
  EXPECT_EQ(max.Words()[0], kLowBound - 1);
}

TEST(FieldElementTest, WordConstructorEnforcesBoundInDebug) {
  // 2^52 violates the magnitude-1 word bound but is legal at magnitude 2.
  EXPECT_DEBUG_DEATH((void)Element{kLowBound}, "");
  const FieldElement<2> ok{kLowBound};
  EXPECT_EQ(ok.Words()[0], kLowBound);
}

TEST(FieldElementTest, ArrayConstructorStoresWords) {
  const Array words{0x8BADF00D0FF1Cull, 0xFACADE0FEEDull, 0xBEEFCACE00ull, 0xC0FFEE123456ull, 0xB0BACAFE0ull};
  const Element x{words};
  EXPECT_EQ(x.Words(), words);
}

TEST(FieldElementTest, ArrayConstructorEnforcesPerWordBoundsInDebug) {
  // Low words have bound m * 2^52...
  Array low_violation{};
  low_violation[2] = kLowBound;
  EXPECT_DEBUG_DEATH((void)Element{low_violation}, "");
  EXPECT_EQ(FieldElement<2>{low_violation}.Words()[2], kLowBound);

  // ...but the top word has the tighter bound m * 2^48.
  Array top_violation{};
  top_violation[kWords - 1] = kTopBound;
  EXPECT_DEBUG_DEATH((void)Element{top_violation}, "");
  EXPECT_EQ(FieldElement<2>{top_violation}.Words()[kWords - 1], kTopBound);
}

TEST(FieldElementTest, CopyAndAssignmentPreserveWords) {
  std::mt19937_64 rng{20260705};
  const auto x = RandomElement<1>(rng);

  const Element copy{x};
  EXPECT_EQ(copy.Words(), x.Words());

  Element assigned;
  assigned = x;
  EXPECT_EQ(assigned.Words(), x.Words());
}

TEST(FieldElementTest, WideningConversionIsImplicitAndExact) {
  // Relabelling to a larger magnitude is free and value-preserving; shrinking the label requires
  // an explicit normalization, so it must not be implicitly convertible.
  static_assert(std::is_convertible_v<FieldElement<1>, FieldElement<3>>);
  static_assert(!std::is_convertible_v<FieldElement<3>, FieldElement<1>>);

  std::mt19937_64 rng{20260705};
  const auto x = RandomElement<1>(rng);
  const FieldElement<3> widened = x;
  EXPECT_EQ(widened.Words(), x.Words());
}

TEST(FieldElementTest, AdditionAddsMagnitudesInTheType) {
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<1>>() + std::declval<FieldElement<1>>()),
                               FieldElement<2>>);
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<2>>() + std::declval<FieldElement<3>>()),
                               FieldElement<5>>);
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<1>>() + std::declval<FieldElement<1>>() +
                                        std::declval<FieldElement<1>>()),
                               FieldElement<3>>);
}

TEST(FieldElementTest, AdditionIsConstexpr) {
  static_assert((Element{1} + Element{2}).Words()[0] == 3);
}

TEST(FieldElementTest, AdditionIsLimbwiseWithoutCarryPropagation) {
  // Max magnitude-1 words: every doubled word exceeds its 52/48-bit lane. The sums must stay in
  // their own words; nothing propagates.
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = kLowBound - 1;
  words[kWords - 1] = kTopBound - 1;

  const Element x{words};
  const auto sum = x + x;
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(sum.Words()[i], 2 * words[i]);
}

TEST(FieldElementTest, AdditionMatchesIntegerAddition) {
  std::mt19937_64 rng{1234};
  for (int trial = 0; trial < 1000; ++trial) {
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<1>(rng);
    EXPECT_EQ(ToInteger(a + b), ToInteger(a) + ToInteger(b));
  }
}

TEST(FieldElementTest, AdditionWithZeroPreservesWords) {
  std::mt19937_64 rng{99};
  const auto x = RandomElement<1>(rng);
  const auto sum = x + Element{};
  EXPECT_EQ(sum.Words(), x.Words());
}

TEST(FieldElementTest, AdditionCommutesAndAssociates) {
  std::mt19937_64 rng{5678};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<1>(rng);
    const auto c = RandomElement<1>(rng);
    EXPECT_EQ((a + b).Words(), (b + a).Words());
    EXPECT_EQ(((a + b) + c).Words(), (a + (b + c)).Words());
  }
}

TEST(FieldElementTest, MixedMagnitudeAdditionMatchesIntegerAddition) {
  std::mt19937_64 rng{424242};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<2>(rng);
    const auto c = RandomElement<5>(rng);
    const auto sum = a + b + c;
    static_assert(std::is_same_v<std::remove_const_t<decltype(sum)>, FieldElement<8>>);
    EXPECT_EQ(ToInteger(sum), ToInteger(a) + ToInteger(b) + ToInteger(c));
  }
}

TEST(FieldElementTest, LShiftScalesMagnitudeInTheType) {
  static_assert(std::is_same_v<decltype(std::declval<Element>().LShift<0>()), FieldElement<1>>);
  static_assert(std::is_same_v<decltype(std::declval<Element>().LShift<1>()), FieldElement<2>>);
  static_assert(std::is_same_v<decltype(std::declval<Element>().LShift<3>()), FieldElement<8>>);
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<3>>().LShift<2>()), FieldElement<12>>);
  static_assert(std::is_same_v<decltype(std::declval<Element>() << 2_c), FieldElement<4>>);
}

TEST(FieldElementTest, LShiftIsConstexpr) {
  static_assert(Element{3}.LShift<2>().Words()[0] == 12);
  static_assert((Element{5} << 3_c).Words()[0] == 40);
}

TEST(FieldElementTest, LShiftShiftsEachWordIndependently) {
  // Max magnitude-1 words: every shifted word overflows its 52/48-bit lane into the headroom
  // bits. Nothing may cross into the next word; the magnitude relabel absorbs the growth.
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = kLowBound - 1;
  words[kWords - 1] = kTopBound - 1;

  const Element x{words};
  const auto shifted = x.LShift<3>();
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(shifted.Words()[i], words[i] << 3);
}

template <int k>
void ExpectShiftMatchesInteger(std::mt19937_64& rng) {
  const auto x = RandomElement<1>(rng);
  const auto shifted = x.LShift<k>();
  static_assert(std::is_same_v<std::remove_const_t<decltype(shifted)>, FieldElement<(1 << k)>>);
  EXPECT_EQ(ToInteger(shifted), ToInteger(x) << k);
}

TEST(FieldElementTest, LShiftMatchesIntegerShift) {
  std::mt19937_64 rng{31337};
  for (int trial = 0; trial < 100; ++trial) {
    ExpectShiftMatchesInteger<1>(rng);
    ExpectShiftMatchesInteger<2>(rng);
    ExpectShiftMatchesInteger<3>(rng);
    ExpectShiftMatchesInteger<8>(rng);
    // k=12 lands exactly on the magnitude cap; low words reach the top of their 64-bit lanes.
    ExpectShiftMatchesInteger<12>(rng);
  }
}

TEST(FieldElementTest, LeftShiftOperatorMatchesLShift) {
  std::mt19937_64 rng{777};
  const auto x = RandomElement<1>(rng);

  const auto via_operator = x << 2_c;
  const auto via_method = x.LShift<2>();
  static_assert(std::is_same_v<decltype(via_operator), decltype(via_method)>);
  EXPECT_EQ(via_operator.Words(), via_method.Words());

  EXPECT_EQ(ToInteger(x << 3_c), ToInteger(x) << 3);
}

TEST(FieldElementTest, PowerOfTwoTimesRoutesThroughShift) {
  std::mt19937_64 rng{2468};
  const auto x = RandomElement<1>(rng);

  static_assert(std::is_same_v<decltype(4_c * x), FieldElement<4>>);
  static_assert(std::is_same_v<decltype(x * 8_c), FieldElement<8>>);
  EXPECT_EQ(ToInteger(4_c * x), ToInteger(x) << 2);
  EXPECT_EQ(ToInteger(x * 8_c), ToInteger(x) << 3);
}

TEST(FieldElementTest, TimesThreeIsLimbwiseAndMatchesIntegerMultiple) {
  static_assert(std::is_same_v<decltype(3_c * std::declval<Element>()), FieldElement<3>>);
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<2>>().Times<3>()), FieldElement<6>>);
  static_assert(Element{5}.Times<3>().Words()[0] == 15);

  // Limbwise: max magnitude-1 words tripled in place, growing into headroom, no cross-word carry.
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = kLowBound - 1;
  words[kWords - 1] = kTopBound - 1;
  const Element max{words};
  const auto tripled = 3_c * max;
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(tripled.Words()[i], 3 * words[i]);

  std::mt19937_64 rng{112358};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement<1>(rng);
    const auto sum = ToInteger(x) + ToInteger(x) + ToInteger(x);
    EXPECT_EQ(ToInteger(3_c * x), sum);
    EXPECT_EQ(ToInteger(x * 3_c), sum);
  }
}

// Dependent context so that an ineligible LShift<k> is a constraint failure, not a hard error.
template <int kM, int k>
concept CanLShift = requires(const FieldElement<kM> x) { x.template LShift<k>(); };

TEST(FieldElementTest, LShiftGuardRejectsShiftsPastMagnitudeCap) {
  static_assert(CanLShift<1, 12>);
  static_assert(!CanLShift<1, 13>);
  static_assert(CanLShift<2, 11>);
  static_assert(!CanLShift<2, 12>);
}

TEST(FieldElementTest, LShiftOnWiderMagnitudeMatchesIntegerShift) {
  std::mt19937_64 rng{8642};
  const auto x = RandomElement<2>(rng);
  const auto shifted = x.LShift<2>();
  static_assert(std::is_same_v<std::remove_const_t<decltype(shifted)>, FieldElement<8>>);
  EXPECT_EQ(ToInteger(shifted), ToInteger(x) << 2);
}

TEST(FieldElementTest, TimesZeroAndOneIdentities) {
  std::mt19937_64 rng{1111};
  const auto x = RandomElement<3>(rng);

  const auto zero = x.Times<0>();
  static_assert(std::is_same_v<std::remove_const_t<decltype(zero)>, FieldElement<0>>);
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(zero.Words()[i], 0u);

  const auto same = x.Times<1>();
  static_assert(std::is_same_v<std::remove_const_t<decltype(same)>, FieldElement<3>>);
  EXPECT_EQ(same.Words(), x.Words());
}

TEST(FieldElementTest, TimesTwoMatchesAddition) {
  std::mt19937_64 rng{2222};
  const auto x = RandomElement<1>(rng);
  static_assert(std::is_same_v<decltype(2_c * x), FieldElement<2>>);
  EXPECT_EQ((2_c * x).Words(), (x + x).Words());
}

TEST(FieldElementTest, TimesGenericOddPathMatchesIntegerMultiple) {
  std::mt19937_64 rng{3333};
  const auto x = RandomElement<1>(rng);
  const auto y = RandomElement<2>(rng);

  static_assert(std::is_same_v<decltype(x * 5_c), FieldElement<5>>);
  static_assert(std::is_same_v<decltype(5_c * y), FieldElement<10>>);

  const auto x5 = ToInteger(x);
  EXPECT_EQ(ToInteger(x * 5_c), x5 + x5 + x5 + x5 + x5);
  const auto y5 = ToInteger(y);
  EXPECT_EQ(ToInteger(5_c * y), y5 + y5 + y5 + y5 + y5);
}

TEST(FieldElementTest, MagnitudeZeroAdditionPreservesMagnitude) {
  std::mt19937_64 rng{4444};
  const auto x = RandomElement<3>(rng);
  const auto sum = x + FieldElement<0>{};
  static_assert(std::is_same_v<std::remove_const_t<decltype(sum)>, FieldElement<3>>);
  EXPECT_EQ(sum.Words(), x.Words());
}

TEST(FieldElementTest, FormulaShapedCompositionMatchesInteger) {
  // The shape the point formulas will take: constant multiples and shifts feeding sums.
  std::mt19937_64 rng{5555};
  const auto a = RandomElement<1>(rng);
  const auto b = RandomElement<1>(rng);
  const auto c = RandomElement<1>(rng);

  const auto composed = 3_c * a + (b << 1_c) + 8_c * c;
  static_assert(std::is_same_v<std::remove_const_t<decltype(composed)>, FieldElement<13>>);

  const auto a3 = ToInteger(a) + ToInteger(a) + ToInteger(a);
  EXPECT_EQ(ToInteger(composed), a3 + (ToInteger(b) << 1) + (ToInteger(c) << 3));
}

TEST(FieldElementTest, TimesNegativeBumpsMagnitudeForNegation) {
  // Times<-k> = -Times<k>: negation adds one to the magnitude, so the declared return type
  // FieldElement<kMagnitude * k> is wrong for k < 0 (it names a negative magnitude).
  static_assert(std::is_same_v<decltype(std::declval<Element>().Times<-3>()), FieldElement<4>>);
}

// ---- Direction: unary negation and subtraction (not yet implemented) ----------------------------
// Contract under test: -x is (m+1)*p_tilde - x limbwise (p_tilde = the 5x52 decomposition of p),
// value-correct mod p, magnitude m+1, no limbwise underflow. a - b = a + (-b), magnitude
// m_a + m_b + 1. Only the type and the value mod p are contractual, not the exact limbs.

const UIntW<256> kZero256 = 0;

UIntW<256> ModP(const UIntW<320>& value) {
  return value.Modulo(secp256k1::p);
}

TEST(FieldElementTest, UnaryMinusBumpsMagnitudeInTheType) {
  static_assert(std::is_same_v<decltype(-std::declval<Element>()), FieldElement<2>>);
  static_assert(std::is_same_v<decltype(-std::declval<FieldElement<3>>()), FieldElement<4>>);
  static_assert(std::is_same_v<decltype(-(-std::declval<Element>())), FieldElement<3>>);
}

TEST(FieldElementTest, UnaryMinusNegatesModuloP) {
  std::mt19937_64 rng{6666};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement<1>(rng);
    EXPECT_EQ(ModP(ToInteger(-x) + ToInteger(x)), kZero256);
  }
}

TEST(FieldElementTest, UnaryMinusIsInvolutionModuloP) {
  std::mt19937_64 rng{7777};
  const auto x = RandomElement<1>(rng);
  EXPECT_EQ(ModP(ToInteger(-(-x))), ModP(ToInteger(x)));
}

TEST(FieldElementTest, UnaryMinusOfZeroIsZeroModuloP) {
  EXPECT_EQ(ModP(ToInteger(-Element{})), kZero256);
}

TEST(FieldElementTest, SubtractionAddsMagnitudesPlusOneInTheType) {
  static_assert(std::is_same_v<decltype(std::declval<Element>() - std::declval<Element>()),
                               FieldElement<3>>);
  static_assert(std::is_same_v<decltype(std::declval<Element>() - std::declval<FieldElement<2>>()),
                               FieldElement<4>>);
}

TEST(FieldElementTest, SubtractionMatchesIntegerSubtractionModuloP) {
  std::mt19937_64 rng{8888};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<2>(rng);
    // (a - b) + b == a (mod p); limbwise arithmetic is exact in Z, so no unsigned wraparound in
    // the oracle either.
    EXPECT_EQ(ModP(ToInteger(a - b) + ToInteger(b)), ModP(ToInteger(a)));
  }
}

TEST(FieldElementTest, SubtractionFromZeroMatchesNegation) {
  std::mt19937_64 rng{9999};
  // Worst case for limbwise underflow: every limb of the subtrahend is at its magnitude-1 max.
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = kLowBound - 1;
  words[kWords - 1] = kTopBound - 1;
  const Element x{words};

  EXPECT_EQ(ModP(ToInteger(Element{} - x) + ToInteger(x)), kZero256);

  const auto y = RandomElement<1>(rng);
  EXPECT_EQ(ModP(ToInteger(y - Element{})), ModP(ToInteger(y)));
  EXPECT_EQ(ModP(ToInteger(y - y)), kZero256);
}

// ---- Direction: operator* and Squared -----------------------------------------------------------
// Contract: full field multiply, output magnitude 2 (the reducing-op fixed state). Only the type
// and the value mod p are contractual; the fold schedule and exact output limbs are not.

// The value of a times b mod p, computed with wide integers as the oracle.
template <int kMA, int kMB>
UIntW<256> ProductModP(const FieldElement<kMA>& a, const FieldElement<kMB>& b) {
  return (ToInteger(a) * ToInteger(b)).Modulo(secp256k1::p);
}

template <int kMA, int kMB>
concept CanMultiply =
    requires(const FieldElement<kMA> a, const FieldElement<kMB> b) { a * b; };

TEST(FieldElementTest, MultiplyReturnsMagnitudeTwo) {
  static_assert(std::is_same_v<decltype(std::declval<Element>() * std::declval<Element>()),
                               FieldElement<2>>);
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<2>>() * std::declval<FieldElement<3>>()),
                               FieldElement<2>>);
  static_assert(std::is_same_v<decltype(std::declval<FieldElement<16>>() * std::declval<FieldElement<16>>()),
                               FieldElement<2>>);
}

TEST(FieldElementTest, MultiplyAdmissionIsBounded) {
  // The exact cap is the implementation's own bound-chain property; tests only pin that the
  // known-good envelope is admitted and absurd magnitudes are rejected.
  static_assert(CanMultiply<1, 1>);
  static_assert(CanMultiply<16, 16>);
  //static_assert(!CanMultiply<1024, 1024>);
}

TEST(FieldElementTest, MultiplyIsConstexpr) {
  // Small values take no folds: the product lands in words[0] regardless of schedule.
  static_assert((Element{2} * Element{3}).Words()[0] == 6);
}

TEST(FieldElementTest, MultiplyMatchesIntegerProductModuloP) {
  std::mt19937_64 rng{271828};
  for (int trial = 0; trial < 200; ++trial) {
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<1>(rng);
    EXPECT_EQ(ModP(ToInteger(a * b)), ProductModP(a, b));
  }
}

TEST(FieldElementTest, MultiplyAtAdmissionCapMatchesIntegerProductModuloP) {
  // Magnitude-16 inputs exercise the fold chunking near its drain bound.
  std::mt19937_64 rng{314159};
  for (int trial = 0; trial < 200; ++trial) {
    const auto a = RandomElement<16>(rng);
    const auto b = RandomElement<16>(rng);
    EXPECT_EQ(ModP(ToInteger(a * b)), ProductModP(a, b));
  }
}

TEST(FieldElementTest, MultiplyMaxLimbsStraddleBothFoldBoundaries) {
  // All limbs at the magnitude-1 max: the product populates every column, the top chunks carry
  // maximal values through the 2^260 R-fold, and the tail exercises the 2^256 c_p-fold.
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = kLowBound - 1;
  words[kWords - 1] = kTopBound - 1;
  const Element max{words};

  EXPECT_EQ(ModP(ToInteger(max * max)), ProductModP(max, max));

  const Element one{1};
  EXPECT_EQ(ModP(ToInteger(max * one)), ModP(ToInteger(max)));
}

TEST(FieldElementTest, MultiplyByZeroAndOne) {
  std::mt19937_64 rng{161803};
  const auto x = RandomElement<1>(rng);
  EXPECT_EQ(ModP(ToInteger(x * Element{1})), ModP(ToInteger(x)));
  EXPECT_EQ(ModP(ToInteger(x * Element{})), kZero256);
}

TEST(FieldElementTest, MultiplyCommutes) {
  // Column sums are symmetric in the operands, so the outputs must agree wordwise, not just mod p.
  std::mt19937_64 rng{577215};
  for (int trial = 0; trial < 50; ++trial) {
    const auto a = RandomElement<2>(rng);
    const auto b = RandomElement<3>(rng);
    EXPECT_EQ((a * b).Words(), (b * a).Words());
  }
}

TEST(FieldElementTest, SquaredMatchesSelfProduct) {
  static_assert(std::is_same_v<decltype(std::declval<Element>().Squared()), FieldElement<2>>);

  std::mt19937_64 rng{141421};
  for (int trial = 0; trial < 200; ++trial) {
    const auto x = RandomElement<1>(rng);
    EXPECT_EQ(ModP(ToInteger(x.Squared())), ProductModP(x, x));
  }

  // Max-limb case for the doubled cross products.
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = kLowBound - 1;
  words[kWords - 1] = kTopBound - 1;
  const Element max{words};
  EXPECT_EQ(ModP(ToInteger(max.Squared())), ProductModP(max, max));
}

// ---- Extreme and boundary magnitudes -------------------------------------------------------------
// Every limb at its magnitude-m maximum. For m = 4096 the low-word bound m * 2^52 wraps to 0 in
// uint64; the -1 wraps it back to 2^64 - 1, which is the correct maximum (unsigned wrap, defined).
template <int kMagnitude>
FieldElement<kMagnitude> MaxElement() {
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = (uint64_t{kMagnitude} << 52) - 1;
  words[kWords - 1] = (uint64_t{kMagnitude} << 48) - 1;
  return FieldElement<kMagnitude>{words};
}

template <int kM>
concept CanSquare = requires(const FieldElement<kM> x) { x.Squared(); };

TEST(FieldElementTest, MultiplyAdmissionBoundaryPairs) {
  // Cap is 8191 (prime) with per-operand class cap 4096, so the largest reachable product is
  // 8190 = 2 * 4095 = 90 * 91. Pairs at and just over the boundary:
  static_assert(CanMultiply<1, 4096>);
  static_assert(CanMultiply<4096, 1>);
  static_assert(CanMultiply<2, 4095>);   // 8190
  static_assert(CanMultiply<4095, 2>);
  static_assert(CanMultiply<3, 2730>);   // 8190
  static_assert(CanMultiply<90, 91>);    // 8190
  static_assert(!CanMultiply<2, 4096>);  // 8192
  static_assert(!CanMultiply<4096, 2>);
  static_assert(!CanMultiply<3, 2731>);  // 8193
  static_assert(!CanMultiply<91, 91>);   // 8281
}

TEST(FieldElementTest, SquaredAdmissionBoundary) {
  // Squared must carry the same admission as *: 90^2 = 8100 <= 8191 < 91^2 = 8281. EXPECT rather
  // than static_assert so a missing constraint on Squared reports as a test failure, not a
  // build break.
  EXPECT_TRUE((CanSquare<90>));
  EXPECT_FALSE((CanSquare<91>));
}

template <int kMA, int kMB>
void ExpectMaxLimbProductExact() {
  const auto a = MaxElement<kMA>();
  const auto b = MaxElement<kMB>();
  EXPECT_EQ(ModP(ToInteger(a * b)), ProductModP(a, b)) << "magnitudes " << kMA << " x " << kMB;
}

TEST(FieldElementTest, MultiplyAtMaximumAdmittedMagnitudesIsExact) {
  // Max limbs at the extreme admitted pairs drive the fold chain to its ceilings: the largest
  // column sums, the largest chunk-loop remainder (drain), and the largest residual (the binding
  // constraint). In debug, the in-function Asserts and the result's Array-ctor bounds all run at
  // their tightest here.
  ExpectMaxLimbProductExact<1, 4096>();
  ExpectMaxLimbProductExact<4096, 1>();
  ExpectMaxLimbProductExact<2, 4095>();
  ExpectMaxLimbProductExact<4095, 2>();
  ExpectMaxLimbProductExact<3, 2730>();
  ExpectMaxLimbProductExact<90, 91>();
  ExpectMaxLimbProductExact<64, 64>();
}

TEST(FieldElementTest, SquaredAtMaximumAdmittedMagnitudeIsExact) {
  const auto x = MaxElement<90>();
  EXPECT_EQ(ModP(ToInteger(x.Squared())), ProductModP(x, x));
}

TEST(FieldElementTest, MultiplyWithMagnitudeZeroOperand) {
  const auto x = MaxElement<4096>();
  EXPECT_EQ(ModP(ToInteger(x * FieldElement<0>{})), kZero256);
  EXPECT_EQ(ModP(ToInteger(FieldElement<0>{} * FieldElement<0>{})), kZero256);
}

TEST(FieldElementTest, WordConstructorBoundaryAtMagnitudeCap) {
  // Magnitude 4096 admits any uint64 word; magnitude 2048 admits exactly words below 2^63.
  const FieldElement<4096> all_ones{~uint64_t{0}};
  EXPECT_EQ(all_ones.Words()[0], ~uint64_t{0});

  const FieldElement<2048> max_2048{(uint64_t{1} << 63) - 1};
  EXPECT_EQ(max_2048.Words()[0], (uint64_t{1} << 63) - 1);
  EXPECT_DEBUG_DEATH((void)FieldElement<2048>{uint64_t{1} << 63}, "");
}

TEST(FieldElementTest, AdditionAtMagnitudeCapWithMaxLimbs) {
  // 2048 + 2048 = the class cap; every low word sums to 2^64 - 2 with no wraparound.
  const auto x = MaxElement<2048>();
  const auto sum = x + x;
  static_assert(std::is_same_v<std::remove_const_t<decltype(sum)>, FieldElement<4096>>);
  for (int i = 0; i < kWords - 1; ++i) EXPECT_EQ(sum.Words()[i], ~uint64_t{0} - 1);
  EXPECT_EQ(ToInteger(sum), ToInteger(x) + ToInteger(x));
}

TEST(FieldElementTest, LShiftToMagnitudeCapWithMaxLimbs) {
  const auto x = MaxElement<1>();
  const auto shifted = x.LShift<12>();
  static_assert(std::is_same_v<std::remove_const_t<decltype(shifted)>, FieldElement<4096>>);
  for (int i = 0; i < kWords - 1; ++i) EXPECT_EQ(shifted.Words()[i], (kLowBound - 1) << 12);
  EXPECT_EQ(ToInteger(shifted), ToInteger(x) << 12);
}

TEST(FieldElementTest, TimesToMagnitudeCapWithMaxLimbs) {
  // 3 * 1365 = 4095: the largest odd-path Times that stays under the class cap.
  const auto x = MaxElement<1365>();
  const auto tripled = x * 3_c;
  static_assert(std::is_same_v<std::remove_const_t<decltype(tripled)>, FieldElement<4095>>);
  EXPECT_EQ(ToInteger(tripled), ToInteger(x) + ToInteger(x) + ToInteger(x));
}

TEST(FieldElementTest, NegationAtMaximumMagnitude) {
  // -x at magnitude 4095 uses the constant 4096 * p52[i], whose low words sit at 2^64 - 4096 —
  // the closest the negation constants come to uint64 overflow.
  const auto x = MaxElement<4095>();
  const auto negated = -x;
  static_assert(std::is_same_v<std::remove_const_t<decltype(negated)>, FieldElement<4096>>);
  EXPECT_EQ(ModP(ToInteger(negated) + ToInteger(x)), kZero256);

  // Negating a zero of high magnitude yields a representation of 0 = 4096 * p.
  EXPECT_EQ(ModP(ToInteger(-FieldElement<4095>{})), kZero256);
}

TEST(FieldElementTest, SubtractionAtExtremeMagnitudes) {
  // 2047 + 2048 + 1 = the class cap; stresses the magnitude-2049 negation constant plus the
  // limbwise add near the top of the lanes.
  const auto a = MaxElement<2047>();
  const auto b = MaxElement<2048>();
  const auto diff = a - b;
  static_assert(std::is_same_v<std::remove_const_t<decltype(diff)>, FieldElement<4096>>);
  EXPECT_EQ(ModP(ToInteger(diff) + ToInteger(b)), ModP(ToInteger(a)));
}

// ---- Normalization family ------------------------------------------------------------------------
// NormalizeWeak: value preserved mod p; all words in-lane except words[0] < 2^53. -> <2>
// Normalize: THE canonical representative -- value < p, all words in-lane. -> <1>
// NormalizesToZero: bool, true iff the value is congruent to 0 mod p. (Direction: not yet built.)

constexpr uint64_t kM52 = kLowBound - 1;
constexpr uint64_t kM48 = kTopBound - 1;
const uint64_t kCp = secp256k1::c_p.Words()[0];  // 2^256 - p, < 2^33

// The 5x52 decomposition of p, replicated for boundary construction.
Array P52() {
  Array words;
  for (int i = 0; i < kWords; ++i) words[i] = (secp256k1::p >> (52 * i)).LowBits<64>() & kM52;
  return words;
}

template <int kM>
void ExpectNormalizeWeakContract(const FieldElement<kM>& x) {
  const auto weak = x.NormalizeWeak();
  static_assert(std::is_same_v<std::remove_const_t<decltype(weak)>, FieldElement<2>>);
  EXPECT_LT(weak.Words()[0], 2 * kLowBound);
  for (int i = 1; i < 4; ++i) EXPECT_LE(weak.Words()[i], kM52);
  EXPECT_LE(weak.Words()[4], kM48);
  EXPECT_EQ(ModP(ToInteger(weak)), ModP(ToInteger(x)));
}

template <int kM>
void ExpectNormalizeContract(const FieldElement<kM>& x) {
  const auto n = x.Normalize();
  static_assert(std::is_same_v<std::remove_const_t<decltype(n)>, FieldElement<1>>);
  for (int i = 0; i < 4; ++i) EXPECT_LE(n.Words()[i], kM52);
  EXPECT_LE(n.Words()[4], kM48);
  // Canonical means the represented integer IS the residue (in particular < p)...
  EXPECT_EQ(ToInteger(n), ModP(ToInteger(x)).template ZeroExtend<320>());
  // ...and canonicalizing again must be a bitwise no-op.
  EXPECT_EQ(n.Normalize().Words(), n.Words());
}

TEST(FieldElementTest, NormalizeWeakContractAcrossMagnitudes) {
  std::mt19937_64 rng{60601};
  for (int trial = 0; trial < 100; ++trial) {
    ExpectNormalizeWeakContract(RandomElement<1>(rng));
    ExpectNormalizeWeakContract(RandomElement<2>(rng));
    ExpectNormalizeWeakContract(RandomElement<90>(rng));
    ExpectNormalizeWeakContract(RandomElement<4095>(rng));
  }
  // Max limbs at the maximum weak-normalizable magnitude: the internal overflow reaches its
  // Assert(< 4096) ceiling.
  ExpectNormalizeWeakContract(MaxElement<4095>());
}

TEST(FieldElementTest, NormalizeWeakIsConstexpr) {
  static_assert(Element{5}.NormalizeWeak().Words()[0] == 5);
}

TEST(FieldElementTest, NormalizeContractAcrossMagnitudes) {
  std::mt19937_64 rng{60602};
  for (int trial = 0; trial < 100; ++trial) {
    ExpectNormalizeContract(RandomElement<1>(rng));
    ExpectNormalizeContract(RandomElement<2>(rng));
    ExpectNormalizeContract(RandomElement<90>(rng));
    ExpectNormalizeContract(RandomElement<4095>(rng));
  }
  ExpectNormalizeContract(MaxElement<4095>());
}

TEST(FieldElementTest, NormalizeIsConstexpr) {
  static_assert(Element{7}.Normalize().Words()[0] == 7);
  // p canonicalizes to zero at compile time.
  static_assert((-FieldElement<0>{}).Normalize().Words()[0] == 0);
}

TEST(FieldElementTest, NormalizeReducesMultiplesOfP) {
  // -(zero of magnitude k-1) has words k * p52: the value k*p, exercising k up to the cap.
  EXPECT_EQ((-FieldElement<0>{}).Normalize().Words(), Array{});     // p
  EXPECT_EQ((-FieldElement<1>{}).Normalize().Words(), Array{});     // 2p
  EXPECT_EQ((-FieldElement<4>{}).Normalize().Words(), Array{});     // 5p
  EXPECT_EQ((-FieldElement<4094>{}).Normalize().Words(), Array{});  // 4095p
}

TEST(FieldElementTest, NormalizeBoundaryValuesAroundP) {
  const Array p_limbs = P52();

  Array p_minus_1 = p_limbs;
  p_minus_1[0] -= 1;
  EXPECT_EQ(Element{p_minus_1}.Normalize().Words(), p_minus_1);  // already canonical, bit-identical

  EXPECT_EQ(Element{p_limbs}.Normalize().Words(), Array{});  // exactly p -> 0

  Array p_plus_1 = p_limbs;
  p_plus_1[0] += 1;
  const Array one{1, 0, 0, 0, 0};
  EXPECT_EQ(Element{p_plus_1}.Normalize().Words(), one);

  // 2^256 - 1 (all lanes at max) -> c_p - 1.
  const Array all_max{kM52, kM52, kM52, kM52, kM48};
  const Array expected{kCp - 1, 0, 0, 0, 0};
  EXPECT_EQ(Element{all_max}.Normalize().Words(), expected);
}

TEST(FieldElementTest, NormalizeTopOverflowPath) {
  // Value 2^257 - 1: the weak-normalized form is 2^256 - 1 + c_p >= 2^256, the only way to
  // drive Normalize's first overflow bit. Expected: (2^257 - 1) mod p = 2*c_p - 1.
  const Array words{kM52, kM52, kM52, kM52, kTopBound + kM48};
  const FieldElement<2> x{words};
  const Array expected{2 * kCp - 1, 0, 0, 0, 0};
  EXPECT_EQ(x.Normalize().Words(), expected);
  EXPECT_EQ(ToInteger(x.Normalize()), ModP(ToInteger(x)).ZeroExtend<320>());
}

TEST(FieldElementTest, NormalizeAfterMultiplyMatchesOracle) {
  std::mt19937_64 rng{60603};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<2>(rng);
    EXPECT_EQ(ToInteger((a * b).Normalize()), ProductModP(a, b).ZeroExtend<320>());
  }
}

// ---- NormalizesToZero (direction: not yet implemented) --------------------------------------------

TEST(FieldElementTest, NormalizesToZeroReturnsBool) {
  static_assert(std::is_same_v<decltype(std::declval<Element>().NormalizesToZero()), bool>);
}

TEST(FieldElementTest, NormalizesToZeroOnZeroRepresentations) {
  EXPECT_TRUE(Element{}.NormalizesToZero());
  EXPECT_TRUE(FieldElement<2>{}.NormalizesToZero());
  EXPECT_TRUE((-FieldElement<0>{}).NormalizesToZero());     // p
  EXPECT_TRUE((-FieldElement<1>{}).NormalizesToZero());     // 2p
  EXPECT_TRUE((-FieldElement<4094>{}).NormalizesToZero());  // 4095p

  // Every difference x - x is a computed zero (arrives as a multiple of p, never as zero limbs).
  std::mt19937_64 rng{60604};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement<2>(rng);
    EXPECT_TRUE((x - x).NormalizesToZero());
  }
}

TEST(FieldElementTest, NormalizesToZeroRejectsNonZero) {
  EXPECT_FALSE(Element{1}.NormalizesToZero());

  const Array p_limbs = P52();
  Array p_minus_1 = p_limbs;
  p_minus_1[0] -= 1;
  EXPECT_FALSE(Element{p_minus_1}.NormalizesToZero());
  Array p_plus_1 = p_limbs;
  p_plus_1[0] += 1;
  EXPECT_FALSE(Element{p_plus_1}.NormalizesToZero());

  // Values congruent to c_p (nonzero): c_p itself, and 2^256 which folds to it.
  EXPECT_FALSE(Element{kCp}.NormalizesToZero());
  EXPECT_FALSE((FieldElement<2>{Array{0, 0, 0, 0, kTopBound}}).NormalizesToZero());

  std::mt19937_64 rng{60605};
  for (int trial = 0; trial < 100; ++trial) {
    EXPECT_FALSE(RandomElement<2048>(rng).NormalizesToZero());  // random hits 0 mod p with prob ~2^-256
  }
}

TEST(FieldElementTest, NormalizesToZeroAgreesWithNormalize) {
  std::mt19937_64 rng{60606};
  for (int trial = 0; trial < 200; ++trial) {
    const auto x = RandomElement<90>(rng);
    EXPECT_EQ(x.NormalizesToZero(), x.Normalize().Words() == Array{});
    const auto y = RandomElement<2>(rng);
    EXPECT_EQ((y - y).NormalizesToZero(), (y - y).Normalize().Words() == Array{});
  }
}

// ---- Pack / Unpack and Fp differentials -----------------------------------------------------------

UIntW<256> RandomCanonical(std::mt19937_64& rng) {
  std::array<uint64_t, 4> words;
  for (auto& w : words) w = rng();
  return UIntW<256>{words}.Modulo(secp256k1::p);
}

TEST(FieldElementTest, UnpackSplitsCanonicalValuesExactly) {
  static_assert(Element{UIntW<256>{7}}.Words()[0] == 7);  // unpack ctor is constexpr

  // Single bits at every 52-bit chunk seam and 64-bit word seam catch shift/mask typos.
  for (int bit : {0, 51, 52, 63, 64, 103, 104, 127, 128, 155, 156, 191, 192, 207, 208, 255}) {
    const UIntW<256> value = UIntW<256>{1} << bit;
    const Element x{value};
    EXPECT_EQ(ToInteger(x), value.ZeroExtend<320>()) << "bit " << bit;
    for (int i = 0; i < 4; ++i) EXPECT_LE(x.Words()[i], kM52);
    EXPECT_LE(x.Words()[4], kM48);
  }

  std::mt19937_64 rng{70701};
  for (int trial = 0; trial < 100; ++trial) {
    const UIntW<256> value = RandomCanonical(rng);
    EXPECT_EQ(ToInteger(Element{value}), value.ZeroExtend<320>());
  }
}

TEST(FieldElementTest, PackUnpackRoundTripIsIdentity) {
  const Array p_limbs = P52();
  Array pm1 = p_limbs;
  pm1[0] -= 1;
  EXPECT_EQ(Element{pm1}.Pack(), secp256k1::p - UIntW<256>{1});

  EXPECT_EQ(Element{}.Pack(), UIntW<256>{0});
  EXPECT_EQ(Element{UIntW<256>{1}}.Pack(), UIntW<256>{1});

  std::mt19937_64 rng{70702};
  for (int trial = 0; trial < 100; ++trial) {
    const UIntW<256> value = RandomCanonical(rng);
    EXPECT_EQ(Element{value}.Pack(), value);
  }
}

TEST(FieldElementTest, PackNormalizesNonCanonicalInputs) {
  EXPECT_EQ((-FieldElement<0>{}).Pack(), UIntW<256>{0});     // p
  EXPECT_EQ((-FieldElement<4094>{}).Pack(), UIntW<256>{0});  // 4095p

  std::mt19937_64 rng{70703};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement<4095>(rng);
    EXPECT_EQ(x.Pack(), ModP(ToInteger(x)));
    const auto a = RandomElement<1>(rng);
    const auto b = RandomElement<2>(rng);
    EXPECT_EQ((a * b).Pack(), ProductModP(a, b));
  }
}

TEST(FieldElementTest, UnpackRequiresCanonicalInDebug) {
  // Contract direction (Fp parity): unpack asserts value < p, keeping pack-unpack a bit identity.
  // If instead >= p inputs should be accepted as weak representations, delete this test and
  // document that Pack(Unpack(x)) == x mod p.
  EXPECT_DEBUG_DEATH((void)Element{secp256k1::p}, "");
  EXPECT_DEBUG_DEATH((void)Element{secp256k1::p + UIntW<256>{1}}, "");
}

// ---- Differentials against Fp, the canonical reference implementation -----------------------------

using FpRef = Fp<secp256k1::kBits, secp256k1::p>;

TEST(FieldElementTest, FieldOpsMatchFpReference) {
  std::mt19937_64 rng{70704};
  for (int trial = 0; trial < 200; ++trial) {
    const UIntW<256> va = RandomCanonical(rng);
    const UIntW<256> vb = RandomCanonical(rng);
    const Element a{va}, b{vb};
    const FpRef fa{va}, fb{vb};

    EXPECT_EQ((a + b).Pack(), (fa + fb).x);
    EXPECT_EQ((a - b).Pack(), (fa - fb).x);
    EXPECT_EQ((-a).Pack(), (-fa).x);
    EXPECT_EQ((a * b).Pack(), (fa * fb).x);
    EXPECT_EQ(a.Squared().Pack(), fa.Squared().x);
    EXPECT_EQ((3_c * a).Pack(), (3_c * fa).x);
    EXPECT_EQ((a << 3_c).Pack(), (fa << 3_c).x);
  }
}

// A formula-shaped chain in the style of the point doubling/add formulas: products of sums and
// differences of scaled squarings. Written once, generically; FieldElement runs it with static
// magnitudes (peaking at 52, products at 312), Fp runs it canonically. Any divergence in
// magnitude bookkeeping, folds, or normalization shows up as a Pack mismatch.
template <class F>
auto FormulaChain(const F& x, const F& y, const F& z) {
  const auto t0 = x.Squared();
  const auto m = 3_c * t0;
  const auto s = ((x + y).Squared() - t0 - y.Squared()) << 1_c;
  const auto x3 = m.Squared() - (s << 1_c);
  const auto y3 = m * (s - x3) - 8_c * y.Squared().Squared();
  return y3 * z;
}

TEST(FieldElementTest, FormulaChainMatchesFpReference) {
  std::mt19937_64 rng{70705};
  for (int trial = 0; trial < 100; ++trial) {
    const UIntW<256> vx = RandomCanonical(rng);
    const UIntW<256> vy = RandomCanonical(rng);
    const UIntW<256> vz = RandomCanonical(rng);
    const auto element_result = FormulaChain(Element{vx}, Element{vy}, Element{vz});
    const auto fp_result = FormulaChain(FpRef{vx}, FpRef{vy}, FpRef{vz});
    EXPECT_EQ(element_result.Pack(), fp_result.x);
  }
}

}  // namespace
}  // namespace hornet::crypto::ecdsa
