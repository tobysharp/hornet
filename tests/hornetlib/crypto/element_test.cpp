// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

// Tests for the 5x52 FieldElement, runtime-magnitude shape: a single non-templated class; the
// magnitude is a checked-build-only member (kCheckMagnitudes), negation carries its magnitude
// bound as a template annotation (Negate<kM>), and all value contracts are pinned against exact
// wide-integer oracles and the Fp reference. Checked-mode contracts (annotation validation,
// admission throws) are gated on kCheckMagnitudes and lie dormant in unchecked builds.

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <random>
#include <stdexcept>
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

using Element = FieldElement;
using Array = Element::Array;
using FpRef = Fp<secp256k1::kBits, secp256k1::p>;

constexpr int kWords = Element::kWords;
// Magnitude invariant: words[0..3] < m * 2^52 and words[4] < m * 2^48.
constexpr uint64_t kLowBound = uint64_t{1} << 52;
constexpr uint64_t kTopBound = uint64_t{1} << 48;
constexpr uint64_t kM52 = kLowBound - 1;
constexpr uint64_t kM48 = kTopBound - 1;
const uint64_t kCp = secp256k1::c_p.Words()[0];  // 2^256 - p, < 2^33

const UIntW<256> kZero256 = 0;

// The unchecked element must remain a pure 5x52 POD: the checks member is empty and vanishes.
static_assert(kCheckMagnitudes || sizeof(Element) == kWords * sizeof(uint64_t));

// The integer represented by the limb vector: sum of words[i] * 2^(52*i). Limbwise arithmetic is
// exact integer arithmetic, so this is the oracle for all value tests.
UIntW<320> ToInteger(const Element& x) {
  UIntW<320> sum = 0;
  for (int i = 0; i < kWords; ++i) sum = sum + (UIntW<320>{x.Words()[i]} << (52 * i));
  return sum;
}

UIntW<256> ModP(const UIntW<320>& value) {
  return value.Modulo(secp256k1::p);
}

// The 5x52 decomposition of p, replicated for boundary construction.
Array P52() {
  Array words;
  for (int i = 0; i < kWords; ++i) words[i] = (secp256k1::p >> (52 * i)).LowBits<64>() & kM52;
  return words;
}

// The value k*p as limbs k*p52[i]: a nonzero-limb representation of zero at magnitude k.
Element KTimesP(int k) {
  Array words = P52();
  for (auto& w : words) w *= uint64_t(k);
  return {words, k};
}

// Random limbs within the magnitude-m bounds. Requires m <= 4095 (the modulus wraps at 4096).
Element RandomElement(std::mt19937_64& rng, int magnitude = 1) {
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = rng() % (uint64_t(magnitude) << 52);
  words[kWords - 1] = rng() % (uint64_t(magnitude) << 48);
  return {words, magnitude};
}

// Every limb at its magnitude-m maximum. For m = 4096 the bound wraps to 0 in uint64; the -1
// wraps it back to 2^64 - 1, the correct maximum.
Element MaxElement(int magnitude) {
  Array words;
  for (int i = 0; i < kWords - 1; ++i) words[i] = (uint64_t(magnitude) << 52) - 1;
  words[kWords - 1] = (uint64_t(magnitude) << 48) - 1;
  return {words, magnitude};
}

UIntW<256> RandomCanonical(std::mt19937_64& rng) {
  std::array<uint64_t, 4> words;
  for (auto& w : words) w = rng();
  return UIntW<256>{words}.Modulo(secp256k1::p);
}

// Annotated-subtraction helpers so shared formula chains spell subtraction identically over
// FieldElement (explicit Negate<kM> annotation) and Fp (plain operators, annotation ignored).
template <int kM> Element Neg(const Element& x) { return x.template Negate<kM>(); }
template <int kM> FpRef Neg(const FpRef& x) { return -x; }
template <int kM, class F> F Sub(const F& a, const F& b) { return a + Neg<kM>(b); }

// ---- Construction --------------------------------------------------------------------------------

TEST(FieldElementTest, DefaultConstructorIsZero) {
  constexpr Element zero{};
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(zero.Words()[i], 0u);
}

TEST(FieldElementTest, WordConstructorSetsLowWord) {
  const Element x{0xABCDEFull};
  EXPECT_EQ(x.Words()[0], 0xABCDEFull);
  for (int i = 1; i < kWords; ++i) EXPECT_EQ(x.Words()[i], 0u);

  // The largest value that stays entirely in word 0.
  const Element max{kLowBound - 1};
  EXPECT_EQ(max.Words()[0], kLowBound - 1);
}

TEST(FieldElementTest, WordConstructorIsTotalAndSplitsAtTheSeam) {
  // Any uint64 is a valid magnitude-1 element: the value splits across words 0/1 at the
  // 52-bit seam, so no bound to enforce and no precondition.
  const Element split{kLowBound};  // 2^52
  EXPECT_EQ(split.Words()[0], 0u);
  EXPECT_EQ(split.Words()[1], 1u);

  const Element all_ones{~uint64_t{0}};
  EXPECT_EQ(all_ones.Words()[0], kLowBound - 1);
  EXPECT_EQ(all_ones.Words()[1], (uint64_t{1} << 12) - 1);
  for (int i = 2; i < kWords; ++i) EXPECT_EQ(all_ones.Words()[i], 0u);
  EXPECT_EQ(ToInteger(all_ones), UIntW<320>{~uint64_t{0}});

  static_assert(Element{uint64_t{1} << 52}.Words()[1] == 1);  // split is constexpr
}

TEST(FieldElementTest, ArrayConstructorStoresWords) {
  const Array words{0x8BADF00D0FF1Cull, 0xFACADE0FEEDull, 0xBEEFCACE00ull, 0xC0FFEE123456ull, 0xB0BACAFE0ull};
  const Element x{words};
  EXPECT_EQ(x.Words(), words);
}

TEST(FieldElementTest, ArrayConstructorValidatesMagnitudeWhenChecking) {
  if constexpr (kCheckMagnitudes) {
    // Low words have bound m * 2^52, the top word m * 2^48.
    Array low_violation{};
    low_violation[2] = kLowBound;
    EXPECT_THROW((void)Element(low_violation, 1), std::out_of_range);
    EXPECT_EQ(Element(low_violation, 2).Words()[2], kLowBound);

    Array top_violation{};
    top_violation[kWords - 1] = kTopBound;
    EXPECT_THROW((void)Element(top_violation, 1), std::out_of_range);
    EXPECT_EQ(Element(top_violation, 2).Words()[kWords - 1], kTopBound);

    // The class magnitude cap.
    EXPECT_THROW((void)Element(Array{}, Element::kMaxMagnitude + 1), std::out_of_range);
    EXPECT_NO_THROW((void)Element(Array{}, Element::kMaxMagnitude));
  }
}

TEST(FieldElementTest, CopyAndAssignmentPreserveWords) {
  std::mt19937_64 rng{20260705};
  const auto x = RandomElement(rng);

  const Element copy{x};
  EXPECT_EQ(copy.Words(), x.Words());

  Element assigned;
  assigned = x;
  EXPECT_EQ(assigned.Words(), x.Words());
}

// ---- Addition ------------------------------------------------------------------------------------

TEST(FieldElementTest, AdditionIsConstexpr) {
  static_assert((Element{1} + Element{2}).Words()[0] == 3);
}

TEST(FieldElementTest, AdditionIsLimbwiseWithoutCarryPropagation) {
  // Max magnitude-1 words: every doubled word exceeds its 52/48-bit lane. The sums must stay in
  // their own words; nothing propagates.
  const auto x = MaxElement(1);
  const auto sum = x + x;
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(sum.Words()[i], 2 * x.Words()[i]);
}

TEST(FieldElementTest, AdditionMatchesIntegerAddition) {
  std::mt19937_64 rng{1234};
  for (int trial = 0; trial < 1000; ++trial) {
    const auto a = RandomElement(rng);
    const auto b = RandomElement(rng);
    EXPECT_EQ(ToInteger(a + b), ToInteger(a) + ToInteger(b));
  }
}

TEST(FieldElementTest, AdditionWithZeroPreservesWords) {
  std::mt19937_64 rng{99};
  const auto x = RandomElement(rng);
  const auto sum = x + Element{};
  EXPECT_EQ(sum.Words(), x.Words());
}

TEST(FieldElementTest, AdditionCommutesAndAssociates) {
  std::mt19937_64 rng{5678};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement(rng);
    const auto b = RandomElement(rng);
    const auto c = RandomElement(rng);
    EXPECT_EQ((a + b).Words(), (b + a).Words());
    EXPECT_EQ(((a + b) + c).Words(), (a + (b + c)).Words());
  }
}

TEST(FieldElementTest, MixedMagnitudeAdditionMatchesIntegerAddition) {
  std::mt19937_64 rng{424242};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement(rng, 1);
    const auto b = RandomElement(rng, 2);
    const auto c = RandomElement(rng, 5);
    EXPECT_EQ(ToInteger(a + b + c), ToInteger(a) + ToInteger(b) + ToInteger(c));
  }
}

TEST(FieldElementTest, AdditionAtLargeMagnitudesWithMaxLimbs) {
  // 2048 + 2048 = the class cap; every low word sums to 2^64 - 2 with no wraparound.
  const auto x = MaxElement(2048);
  const auto sum = x + x;
  for (int i = 0; i < kWords - 1; ++i) EXPECT_EQ(sum.Words()[i], ~uint64_t{0} - 1);
  EXPECT_EQ(ToInteger(sum), ToInteger(x) + ToInteger(x));

  if constexpr (kCheckMagnitudes) {
    // Past the cap the result construction must reject.
    EXPECT_THROW((void)(MaxElement(2048) + MaxElement(2049)), std::out_of_range);
  }
}

// ---- LShift and Times ----------------------------------------------------------------------------

TEST(FieldElementTest, LShiftIsConstexpr) {
  static_assert(Element{3}.LShift<2>().Words()[0] == 12);
  static_assert((Element{5} << 3_c).Words()[0] == 40);
}

TEST(FieldElementTest, LShiftShiftsEachWordIndependently) {
  const auto x = MaxElement(1);
  const auto shifted = x.LShift<3>();
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(shifted.Words()[i], x.Words()[i] << 3);
}

template <int k>
void ExpectShiftMatchesInteger(std::mt19937_64& rng) {
  const auto x = RandomElement(rng);
  EXPECT_EQ(ToInteger(x.LShift<k>()), ToInteger(x) << k);
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
  const auto x = RandomElement(rng);
  EXPECT_EQ((x << 2_c).Words(), x.LShift<2>().Words());
  EXPECT_EQ(ToInteger(x << 3_c), ToInteger(x) << 3);
}

TEST(FieldElementTest, PowerOfTwoTimesRoutesThroughShift) {
  std::mt19937_64 rng{2468};
  const auto x = RandomElement(rng);
  EXPECT_EQ(ToInteger(4_c * x), ToInteger(x) << 2);
  EXPECT_EQ(ToInteger(x * 8_c), ToInteger(x) << 3);
}

TEST(FieldElementTest, TimesThreeIsLimbwiseAndMatchesIntegerMultiple) {
  static_assert(Element{5}.Times<3>().Words()[0] == 15);

  // Limbwise: max magnitude-1 words tripled in place, growing into headroom, no cross-word carry.
  const auto max = MaxElement(1);
  const auto tripled = 3_c * max;
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(tripled.Words()[i], 3 * max.Words()[i]);

  std::mt19937_64 rng{112358};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement(rng);
    const auto sum = ToInteger(x) + ToInteger(x) + ToInteger(x);
    EXPECT_EQ(ToInteger(3_c * x), sum);
    EXPECT_EQ(ToInteger(x * 3_c), sum);
  }
}

TEST(FieldElementTest, TimesZeroAndOneIdentities) {
  std::mt19937_64 rng{1111};
  const auto x = RandomElement(rng, 3);

  const auto zero = x.Times<0>();
  for (int i = 0; i < kWords; ++i) EXPECT_EQ(zero.Words()[i], 0u);

  const auto same = x.Times<1>();
  EXPECT_EQ(same.Words(), x.Words());
}

TEST(FieldElementTest, TimesTwoMatchesAddition) {
  std::mt19937_64 rng{2222};
  const auto x = RandomElement(rng);
  EXPECT_EQ((2_c * x).Words(), (x + x).Words());
}

TEST(FieldElementTest, TimesGenericOddPathMatchesIntegerMultiple) {
  std::mt19937_64 rng{3333};
  const auto x = RandomElement(rng);
  const auto x1 = ToInteger(x);
  EXPECT_EQ(ToInteger(x * 5_c), x1 + x1 + x1 + x1 + x1);
}

TEST(FieldElementTest, TimesMinusOneMatchesNegationOnCanonical) {
  std::mt19937_64 rng{4444};
  const auto x = RandomElement(rng);
  EXPECT_EQ(ModP(ToInteger(x.Times<-1>()) + ToInteger(x)), kZero256);
}

// ---- Negation ------------------------------------------------------------------------------------
// Negation is the one operation whose arithmetic consumes a magnitude (the dominating constant
// (m+1)*p52): the bound is a template annotation, Negate<kM>, valid for true magnitude <= kM.
// Unary operator- is the kM = 1 (canonical operand) case.

TEST(FieldElementTest, UnaryMinusNegatesCanonicalModuloP) {
  std::mt19937_64 rng{6666};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement(rng);
    EXPECT_EQ(ModP(ToInteger(-x) + ToInteger(x)), kZero256);
  }
  const auto max = MaxElement(1);
  EXPECT_EQ(ModP(ToInteger(-max) + ToInteger(max)), kZero256);
}

TEST(FieldElementTest, UnaryMinusIsLimbwiseInvolution) {
  // 2*p52 - (2*p52 - w) = w exactly: double negation is bit-identical, not just congruent.
  std::mt19937_64 rng{7777};
  const auto x = RandomElement(rng);
  EXPECT_EQ((-(-x)).Words(), x.Words());
}

TEST(FieldElementTest, UnaryMinusOfZeroIsZeroModuloP) {
  EXPECT_EQ(ModP(ToInteger(-Element{})), kZero256);
}

template <int kM>
void ExpectAnnotatedNegationMatchesOracle(const Element& x) {
  const auto negated = x.template Negate<kM>();
  EXPECT_EQ(ModP(ToInteger(negated) + ToInteger(x)), kZero256);
}

TEST(FieldElementTest, NegateAnnotatedMatchesOracleAcrossMagnitudes) {
  std::mt19937_64 rng{8888};
  ExpectAnnotatedNegationMatchesOracle<2>(RandomElement(rng, 2));
  ExpectAnnotatedNegationMatchesOracle<90>(RandomElement(rng, 90));
  ExpectAnnotatedNegationMatchesOracle<2048>(RandomElement(rng, 2048));
  ExpectAnnotatedNegationMatchesOracle<4095>(MaxElement(4095));
  // A generous annotation (kM above the true magnitude) is still value-correct.
  ExpectAnnotatedNegationMatchesOracle<90>(RandomElement(rng, 2));
}

TEST(FieldElementTest, NegateValidatesAnnotationWhenChecking) {
  if constexpr (kCheckMagnitudes) {
    // Direction: an under-sized annotation is a value bug in unchecked builds; the checked
    // variant must reject it.
    EXPECT_THROW((void)MaxElement(2).Negate<1>(), std::out_of_range);
    EXPECT_NO_THROW((void)MaxElement(2).Negate<2>());
  }
}

// ---- Subtraction ---------------------------------------------------------------------------------

TEST(FieldElementTest, AnnotatedSubtractionMatchesOracleAcrossMagnitudes) {
  std::mt19937_64 rng{9999};
  const auto a = RandomElement(rng, 2);
  const auto b = RandomElement(rng, 90);
  EXPECT_EQ(ModP(ToInteger(Sub<90>(a, b)) + ToInteger(b)), ModP(ToInteger(a)));

  // Extreme magnitudes: subtrahend at 2048 with max limbs.
  const auto big_a = MaxElement(2047);
  const auto big_b = MaxElement(2048);
  EXPECT_EQ(ModP(ToInteger(Sub<2048>(big_a, big_b)) + ToInteger(big_b)), ModP(ToInteger(big_a)));
}

TEST(FieldElementTest, SubtractionWithCanonicalSubtrahendMatchesOracle) {
  // operator- delegates to unary minus: valid whenever the subtrahend is canonical-bounded.
  std::mt19937_64 rng{10101};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement(rng, 5);
    const auto b = RandomElement(rng);
    EXPECT_EQ(ModP(ToInteger(a - b) + ToInteger(b)), ModP(ToInteger(a)));
  }
  const auto y = RandomElement(rng);
  EXPECT_EQ(ModP(ToInteger(y - y)), kZero256);
}

TEST(FieldElementTest, DirectionDefaultSubtractionAdmitsProductSubtrahends) {
  // Direction (settled design, not yet implemented): the point formulas subtract products, which
  // have magnitude 2 -- the default annotation on operator- must admit them. Deterministic
  // boundary: a magnitude-2 subtrahend with all limbs at 2^53 - 1 underflows a Negate<1>
  // constant, so this fails (wrong value unchecked, throw checked) until the default is raised.
  const auto a = Element{1};
  Array limbs;
  for (int i = 0; i < kWords - 1; ++i) limbs[i] = (uint64_t{2} << 52) - 1;
  limbs[kWords - 1] = (uint64_t{2} << 48) - 1;
  const Element b{limbs, 2};
  EXPECT_EQ(ModP(ToInteger(a - b) + ToInteger(b)), ModP(ToInteger(a)));
}

// ---- Equality ------------------------------------------------------------------------------------
// Semantic equality: a == b iff both represent the same residue mod p. Only operator== is
// declared; != and reversed operand orders come from C++20 rewriting. x == 0 is the hot spelling
// and is pure NormalizesToZero (no subtraction).

TEST(FieldElementTest, EqualityIsConstexpr) {
  static_assert(Element{7} == Element{7});
  static_assert(Element{7} != Element{8});
  static_assert(Element{7} == 7);
  static_assert(Element{7} != 0);
}

TEST(FieldElementTest, EqualityWithZeroAcrossRepresentations) {
  EXPECT_TRUE(Element{} == 0);
  EXPECT_TRUE(KTimesP(1) == 0);
  EXPECT_TRUE(KTimesP(4095) == 0);
  EXPECT_TRUE(Element{P52()} == 0);

  std::mt19937_64 rng{60607};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement(rng);
    EXPECT_TRUE((x - x) == 0);
    EXPECT_FALSE(x == 0);  // random hits 0 mod p with prob ~2^-256
  }
}

TEST(FieldElementTest, InequalityWithZeroOnNonZeroValues) {
  EXPECT_TRUE(Element{1} != 0);
  EXPECT_TRUE(Element{kCp} != 0);  // c_p, the fold constant

  Array p_minus_1 = P52();
  p_minus_1[0] -= 1;
  EXPECT_TRUE(Element{p_minus_1} != 0);
  Array p_plus_1 = P52();
  p_plus_1[0] += 1;
  EXPECT_TRUE(Element{p_plus_1} != 0);
}

TEST(FieldElementTest, RewrittenComparisonSpellings) {
  // Reversed operand order and != must both come from the rewritten operator== candidates.
  const auto p_rep = KTimesP(1);  // == 0 mod p
  EXPECT_TRUE(0 == p_rep);
  EXPECT_FALSE(0 != p_rep);
  EXPECT_FALSE(p_rep != 0);

  const Element one{1};
  EXPECT_TRUE(one != 0);
  EXPECT_TRUE(0 != one);
  EXPECT_FALSE(0 == one);
}

TEST(FieldElementTest, EqualityWithSmallConstants) {
  std::mt19937_64 rng{60608};
  const auto y = RandomElement(rng);
  EXPECT_TRUE((Element{5} + (y - y)) == 5);  // 5 plus a multiple of p in the limbs

  Array p_plus_5 = P52();
  p_plus_5[0] += 5;
  EXPECT_TRUE(Element{p_plus_5} == 5);

  EXPECT_FALSE(Element{6} == 5);
  EXPECT_TRUE(Element{6} != 5);
  EXPECT_FALSE(Element{} == 5);
}

TEST(FieldElementTest, EqualityWithLargeWordConstants) {
  EXPECT_TRUE(Element{kLowBound} == kLowBound);  // 2^52, the first two-limb value
  EXPECT_TRUE(Element{~uint64_t{0}} == ~uint64_t{0});
  EXPECT_TRUE(Element{~uint64_t{0}} != ~uint64_t{0} - 1);
}

TEST(FieldElementTest, EqualityIsRepresentationIndependentForCanonicalRhs) {
  std::mt19937_64 rng{60609};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement(rng);
    const auto y = RandomElement(rng);

    EXPECT_TRUE((x + (y - y)) == x);  // lhs limbs offset by a multiple of p
    EXPECT_TRUE(x == -(-x));
    EXPECT_TRUE(x == x.NormalizeWeak());
    EXPECT_TRUE(x == x.Normalize());
  }
}

TEST(FieldElementTest, DirectionEqualityAdmitsSmallMagnitudeRhs) {
  // Direction (settled design, not yet implemented): the point formulas compare against sums of
  // products (IsOnCurve rhs is magnitude ~4), so == must admit small-magnitude rhs operands.
  // Deterministic with the current canonical-only subtrahend: rhs limbs sit above 2*p52.
  std::mt19937_64 rng{60610};
  const auto x = RandomElement(rng);
  const auto y = RandomElement(rng);
  EXPECT_TRUE(x == (x + (y - y)));
}

TEST(FieldElementTest, EqualityMatchesResidueOracleAgainstCanonicalRhs) {
  std::mt19937_64 rng{60611};
  for (int trial = 0; trial < 200; ++trial) {
    const auto lhs = RandomElement(rng, 90);
    const Element rhs{RandomCanonical(rng)};
    const bool expected = ModP(ToInteger(lhs)) == ModP(ToInteger(rhs));
    EXPECT_EQ(lhs == rhs, expected);
    EXPECT_EQ(lhs != rhs, !expected);
    EXPECT_TRUE(lhs == lhs);
  }
}

// ---- Normalization family ------------------------------------------------------------------------
// NormalizeWeak: value preserved mod p; all words in-lane except words[0] < 2^53.
// Normalize: THE canonical representative -- value < p, all words in-lane.
// NormalizesToZero: true iff the value is congruent to 0 mod p.

void ExpectNormalizeWeakContract(const Element& x) {
  const auto weak = x.NormalizeWeak();
  EXPECT_LT(weak.Words()[0], 2 * kLowBound);
  for (int i = 1; i < 4; ++i) EXPECT_LE(weak.Words()[i], kM52);
  EXPECT_LE(weak.Words()[4], kM48);
  EXPECT_EQ(ModP(ToInteger(weak)), ModP(ToInteger(x)));
}

void ExpectNormalizeContract(const Element& x) {
  const auto n = x.Normalize();
  for (int i = 0; i < 4; ++i) EXPECT_LE(n.Words()[i], kM52);
  EXPECT_LE(n.Words()[4], kM48);
  // Canonical means the represented integer IS the residue (in particular < p)...
  EXPECT_EQ(ToInteger(n), ModP(ToInteger(x)).ZeroExtend<320>());
  // ...and canonicalizing again must be a bitwise no-op.
  EXPECT_EQ(n.Normalize().Words(), n.Words());
}

TEST(FieldElementTest, NormalizeWeakContractAcrossMagnitudes) {
  std::mt19937_64 rng{60601};
  for (int trial = 0; trial < 100; ++trial) {
    ExpectNormalizeWeakContract(RandomElement(rng, 1));
    ExpectNormalizeWeakContract(RandomElement(rng, 2));
    ExpectNormalizeWeakContract(RandomElement(rng, 90));
    ExpectNormalizeWeakContract(RandomElement(rng, 4095));
  }
  // Max limbs at the maximum weak-normalizable magnitude.
  ExpectNormalizeWeakContract(MaxElement(4095));
}

TEST(FieldElementTest, NormalizeWeakIsConstexpr) {
  static_assert(Element{5}.NormalizeWeak().Words()[0] == 5);
}

TEST(FieldElementTest, NormalizeWeakRejectsMagnitudeCapWhenChecking) {
  if constexpr (kCheckMagnitudes) {
    // Direction: at m = 4096 the low limbs span the full uint64 and the single-pass carry
    // accumulator can wrap, so NormalizeWeak's admission is m <= 4095 -- one tighter than the
    // class cap that CheckMagnitudes enforces.
    EXPECT_THROW((void)MaxElement(4096).NormalizeWeak(), std::out_of_range);
    EXPECT_NO_THROW((void)MaxElement(4095).NormalizeWeak());
  }
}

TEST(FieldElementTest, NormalizeContractAcrossMagnitudes) {
  std::mt19937_64 rng{60602};
  for (int trial = 0; trial < 100; ++trial) {
    ExpectNormalizeContract(RandomElement(rng, 1));
    ExpectNormalizeContract(RandomElement(rng, 2));
    ExpectNormalizeContract(RandomElement(rng, 90));
    ExpectNormalizeContract(RandomElement(rng, 4095));
  }
  ExpectNormalizeContract(MaxElement(4095));
}

TEST(FieldElementTest, NormalizeIsConstexpr) {
  static_assert(Element{7}.Normalize().Words()[0] == 7);
  // p canonicalizes to zero at compile time (Negate<0> of zero has limbs p52).
  static_assert(Element{}.Negate<0>().Normalize().Words()[0] == 0);
}

TEST(FieldElementTest, NormalizeReducesMultiplesOfP) {
  EXPECT_EQ(KTimesP(1).Normalize().Words(), Array{});
  EXPECT_EQ(KTimesP(2).Normalize().Words(), Array{});
  EXPECT_EQ(KTimesP(5).Normalize().Words(), Array{});
  EXPECT_EQ(KTimesP(4095).Normalize().Words(), Array{});
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
  const Element x{words, 2};
  const Array expected{2 * kCp - 1, 0, 0, 0, 0};
  EXPECT_EQ(x.Normalize().Words(), expected);
}

TEST(FieldElementTest, NormalizeAfterMultiplyMatchesOracle) {
  std::mt19937_64 rng{60603};
  for (int trial = 0; trial < 100; ++trial) {
    const auto a = RandomElement(rng);
    const auto b = RandomElement(rng, 2);
    EXPECT_EQ(ToInteger((a * b).Normalize()),
              (ToInteger(a) * ToInteger(b)).Modulo(secp256k1::p).ZeroExtend<320>());
  }
}

TEST(FieldElementTest, NormalizesToZeroOnZeroRepresentations) {
  EXPECT_TRUE(Element{}.NormalizesToZero());
  EXPECT_TRUE(KTimesP(1).NormalizesToZero());
  EXPECT_TRUE(KTimesP(2).NormalizesToZero());
  EXPECT_TRUE(KTimesP(4095).NormalizesToZero());

  // Every difference x - x is a computed zero (arrives as a multiple of p, never as zero limbs).
  std::mt19937_64 rng{60604};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement(rng);
    EXPECT_TRUE((x - x).NormalizesToZero());
  }
}

TEST(FieldElementTest, NormalizesToZeroRejectsNonZero) {
  EXPECT_FALSE(Element{1}.NormalizesToZero());

  Array p_minus_1 = P52();
  p_minus_1[0] -= 1;
  EXPECT_FALSE(Element{p_minus_1}.NormalizesToZero());
  Array p_plus_1 = P52();
  p_plus_1[0] += 1;
  EXPECT_FALSE(Element{p_plus_1}.NormalizesToZero());

  // Values congruent to c_p (nonzero): c_p itself, and 2^256 which folds to it.
  EXPECT_FALSE(Element{kCp}.NormalizesToZero());
  EXPECT_FALSE(Element(Array{0, 0, 0, 0, kTopBound}, 2).NormalizesToZero());

  std::mt19937_64 rng{60605};
  for (int trial = 0; trial < 100; ++trial) {
    EXPECT_FALSE(RandomElement(rng, 2048).NormalizesToZero());  // 0 mod p with prob ~2^-256
  }
}

TEST(FieldElementTest, NormalizesToZeroAgreesWithNormalize) {
  std::mt19937_64 rng{60606};
  for (int trial = 0; trial < 200; ++trial) {
    const auto x = RandomElement(rng, 90);
    EXPECT_EQ(x.NormalizesToZero(), x.Normalize().Words() == Array{});
  }
}

// ---- Multiplication and Squared ------------------------------------------------------------------
// Full field multiply, result always magnitude 2 (the insulation property). The value and the
// admission (m_a * m_b <= 8191, runtime-checked) are contractual; the fold schedule is not.

UIntW<256> ProductModP(const Element& a, const Element& b) {
  return (ToInteger(a) * ToInteger(b)).Modulo(secp256k1::p);
}

TEST(FieldElementTest, MultiplyIsConstexpr) {
  static_assert((Element{2} * Element{3}).Words()[0] == 6);
}

TEST(FieldElementTest, MultiplyMatchesIntegerProductModuloP) {
  std::mt19937_64 rng{271828};
  for (int trial = 0; trial < 200; ++trial) {
    const auto a = RandomElement(rng);
    const auto b = RandomElement(rng);
    EXPECT_EQ(ModP(ToInteger(a * b)), ProductModP(a, b));
  }
}

TEST(FieldElementTest, MultiplyAtHighMagnitudesMatchesIntegerProductModuloP) {
  std::mt19937_64 rng{314159};
  for (int trial = 0; trial < 200; ++trial) {
    const auto a = RandomElement(rng, 16);
    const auto b = RandomElement(rng, 16);
    EXPECT_EQ(ModP(ToInteger(a * b)), ProductModP(a, b));
  }
}

void ExpectMaxLimbProductExact(int ma, int mb) {
  const auto a = MaxElement(ma);
  const auto b = MaxElement(mb);
  EXPECT_EQ(ModP(ToInteger(a * b)), ProductModP(a, b)) << "magnitudes " << ma << " x " << mb;
}

TEST(FieldElementTest, MultiplyAtMaximumAdmittedMagnitudesIsExact) {
  // Cap is 8191 (prime) with per-operand class cap 4096, so the largest reachable product is
  // 8190 = 2 * 4095 = 90 * 91. Max limbs at the extreme admitted pairs drive the fold chain to
  // its ceilings.
  ExpectMaxLimbProductExact(1, 4096);
  ExpectMaxLimbProductExact(4096, 1);
  ExpectMaxLimbProductExact(2, 4095);
  ExpectMaxLimbProductExact(4095, 2);
  ExpectMaxLimbProductExact(3, 2730);
  ExpectMaxLimbProductExact(90, 91);
  ExpectMaxLimbProductExact(64, 64);
}

TEST(FieldElementTest, MultiplyRejectsExcessiveProductsWhenChecking) {
  if constexpr (kCheckMagnitudes) {
    EXPECT_THROW((void)(MaxElement(2) * MaxElement(4096)), std::out_of_range);   // 8192
    EXPECT_THROW((void)(MaxElement(3) * MaxElement(2731)), std::out_of_range);   // 8193
    EXPECT_THROW((void)(MaxElement(91) * MaxElement(91)), std::out_of_range);    // 8281
    EXPECT_NO_THROW((void)(MaxElement(90) * MaxElement(91)));                    // 8190
  }
}

TEST(FieldElementTest, MultiplyMaxLimbsStraddleBothFoldBoundaries) {
  const auto max = MaxElement(1);
  EXPECT_EQ(ModP(ToInteger(max * max)), ProductModP(max, max));

  const Element one{1};
  EXPECT_EQ(ModP(ToInteger(max * one)), ModP(ToInteger(max)));
}

TEST(FieldElementTest, MultiplyByZeroAndOne) {
  std::mt19937_64 rng{161803};
  const auto x = RandomElement(rng);
  EXPECT_EQ(ModP(ToInteger(x * Element{1})), ModP(ToInteger(x)));
  EXPECT_EQ(ModP(ToInteger(x * Element{})), kZero256);
}

TEST(FieldElementTest, MultiplyCommutes) {
  // Column sums are symmetric in the operands, so the outputs must agree wordwise, not just mod p.
  std::mt19937_64 rng{577215};
  for (int trial = 0; trial < 50; ++trial) {
    const auto a = RandomElement(rng, 2);
    const auto b = RandomElement(rng, 3);
    EXPECT_EQ((a * b).Words(), (b * a).Words());
  }
}

TEST(FieldElementTest, SquaredMatchesSelfProduct) {
  std::mt19937_64 rng{141421};
  for (int trial = 0; trial < 200; ++trial) {
    const auto x = RandomElement(rng);
    EXPECT_EQ(ModP(ToInteger(x.Squared())), ProductModP(x, x));
  }
  const auto max = MaxElement(90);
  EXPECT_EQ(ModP(ToInteger(max.Squared())), ProductModP(max, max));
}

// ---- Pack / Unpack -------------------------------------------------------------------------------

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
  Array pm1 = P52();
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
  EXPECT_EQ(KTimesP(1).Pack(), UIntW<256>{0});
  EXPECT_EQ(KTimesP(4095).Pack(), UIntW<256>{0});

  std::mt19937_64 rng{70703};
  for (int trial = 0; trial < 100; ++trial) {
    const auto x = RandomElement(rng, 4095);
    EXPECT_EQ(x.Pack(), ModP(ToInteger(x)));
    const auto a = RandomElement(rng);
    const auto b = RandomElement(rng, 2);
    EXPECT_EQ((a * b).Pack(), ProductModP(a, b));
  }
}

TEST(FieldElementTest, UnpackRequiresCanonicalInDebug) {
  // Unpack asserts value < p, keeping pack-unpack a bit identity.
  EXPECT_DEBUG_DEATH((void)Element{secp256k1::p}, "");
  EXPECT_DEBUG_DEATH((void)Element{secp256k1::p + UIntW<256>{1}}, "");
}

// ---- Inverse and division ------------------------------------------------------------------------

TEST(FieldElementTest, InverseIsCanonical) {
  std::mt19937_64 rng{80801};
  const auto x = RandomElement(rng);
  const auto inv = x.Inverse();
  EXPECT_EQ(inv.Normalize().Words(), inv.Words());
  EXPECT_EQ(Element{inv.Pack()}.Words(), inv.Words());
}

TEST(FieldElementTest, InverseTimesSelfIsOne) {
  std::mt19937_64 rng{80802};
  for (int trial = 0; trial < 50; ++trial) {
    const auto x = RandomElement(rng);
    EXPECT_TRUE(x * x.Inverse() == 1);
  }
}

TEST(FieldElementTest, InverseKnownAnswers) {
  EXPECT_TRUE(Element{1}.Inverse() == 1);

  // 2^-1 = (p+1)/2.
  const UIntW<256> half = (secp256k1::p + UIntW<256>{1}) >> 1;
  EXPECT_EQ(Element{2}.Inverse().Pack(), half);

  // p-1 is its own inverse: (p-1)^2 == 1 (mod p).
  const UIntW<256> p_minus_1 = secp256k1::p - UIntW<256>{1};
  EXPECT_EQ(Element{p_minus_1}.Inverse().Pack(), p_minus_1);
}

TEST(FieldElementTest, InverseIsInvolution) {
  std::mt19937_64 rng{80803};
  for (int trial = 0; trial < 20; ++trial) {
    const auto x = RandomElement(rng);
    EXPECT_TRUE(x.Inverse().Inverse() == x);
  }
}

TEST(FieldElementTest, InverseIsRepresentationIndependent) {
  std::mt19937_64 rng{80804};
  for (int trial = 0; trial < 20; ++trial) {
    const auto x = RandomElement(rng);
    const auto y = RandomElement(rng);
    const auto offset = x + (y - y);  // same residue, limbs offset by a multiple of p
    EXPECT_EQ(offset.Inverse().Words(), x.Inverse().Words());
  }

  // Highest normalizable magnitude admits Inverse via the same pack boundary.
  const auto big = RandomElement(rng, 4095);
  EXPECT_TRUE(big * big.Inverse() == 1);
}

TEST(FieldElementTest, InverseMatchesFpReference) {
  std::mt19937_64 rng{80805};
  for (int trial = 0; trial < 50; ++trial) {
    const UIntW<256> v = RandomCanonical(rng);
    if (v == UIntW<256>{0}) continue;
    const auto inv = Element{v}.Inverse();
    const FpRef ref = FpRef{v}.Inverse();
    EXPECT_EQ(inv.Pack(), ref.x);
    EXPECT_EQ(inv.Words(), Element{ref.x}.Words());  // bit-identity pins canonicality
  }
}

TEST(FieldElementTest, InverseOfZeroThrows) {
  EXPECT_THROW(static_cast<void>(Element{}.Inverse()), std::runtime_error);
  // A computed zero: the limbs hold p, not zeros.
  EXPECT_THROW(static_cast<void>(KTimesP(1).Inverse()), std::runtime_error);
}

TEST(FieldElementTest, DivisionMatchesInverseProductAndFp) {
  std::mt19937_64 rng{80806};
  for (int trial = 0; trial < 50; ++trial) {
    const UIntW<256> va = RandomCanonical(rng);
    const UIntW<256> vb = RandomCanonical(rng);
    if (vb == UIntW<256>{0}) continue;
    const Element a{va}, b{vb};
    EXPECT_EQ((a / b).Pack(), (FpRef{va} / FpRef{vb}).x);
    EXPECT_EQ((a / b).Pack(), (a * b.Inverse()).Pack());
  }
}

TEST(FieldElementTest, DivisionByZeroThrows) {
  const Element one{1};
  EXPECT_THROW(static_cast<void>(one / Element{}), std::runtime_error);
}

// ---- SquareRoot ----------------------------------------------------------------------------------

TEST(FieldElementTest, SquareRootMatchesFpReference) {
  std::mt19937_64 rng{80807};
  int residues = 0, non_residues = 0;
  for (int trial = 0; trial < 50; ++trial) {
    const UIntW<256> v = RandomCanonical(rng);
    const auto fe = Element{v}.SquareRoot();
    const auto fp = FpRef{v}.SquareRoot();
    ASSERT_EQ(fe.has_value(), fp.has_value());
    if (fe) {
      EXPECT_EQ(fe->Pack(), fp->x);
      ++residues;
    } else {
      ++non_residues;
    }
  }
  EXPECT_GT(residues, 0);
  EXPECT_GT(non_residues, 0);
}

TEST(FieldElementTest, SquareRootReturnsCanonicalRepresentation) {
  // Contract: the returned root is canonical, so its limb parity IS the residue parity -- the
  // property compressed-point decompression relies on for root selection. Tiny residues are the
  // attacker-reachable case where a y + p representation would flip the parity bit.
  for (uint64_t y : {1ull, 2ull, 3ull, 5ull, 255ull}) {
    const auto root = Element{y}.Squared().SquareRoot();
    ASSERT_TRUE(root.has_value()) << "y=" << y;
    EXPECT_EQ(root->Words(), Element{root->Pack()}.Words()) << "y=" << y;
    const UIntW<256> packed = root->Pack();
    EXPECT_TRUE(packed == UIntW<256>{y} || packed == secp256k1::p - UIntW<256>{y}) << "y=" << y;
  }

  std::mt19937_64 rng{80808};
  for (int trial = 0; trial < 20; ++trial) {
    const auto root = Element{RandomCanonical(rng)}.SquareRoot();
    if (root) EXPECT_EQ(root->Words(), Element{root->Pack()}.Words());
  }
}

TEST(FieldElementTest, SquareRootAdmitsSmallMagnitudeInputs) {
  // The decompression call site feeds y^2 = x^3 + b at magnitude ~3.
  std::mt19937_64 rng{80809};
  for (int trial = 0; trial < 10; ++trial) {
    const Element canonical{RandomCanonical(rng)};
    const Element inflated = canonical + KTimesP(2);  // same residue, magnitude 3
    const auto a = canonical.SquareRoot();
    const auto b = inflated.SquareRoot();
    ASSERT_EQ(a.has_value(), b.has_value());
    if (a) EXPECT_EQ(a->Pack(), b->Pack());
  }
}

// ---- Differentials against Fp, the canonical reference implementation ---------------------------

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
// differences of scaled squarings, subtraction spelled through the annotated Sub helper so the
// same source runs over FieldElement (annotations) and Fp (plain operators).
template <class F>
auto FormulaChain(const F& x, const F& y, const F& z) {
  const auto t0 = x.Squared();
  const auto m = 3_c * t0;
  const auto s = Sub<2>(Sub<2>((x + y).Squared(), t0), y.Squared()) << 1_c;
  const auto x3 = Sub<32>(m.Squared(), s << 1_c);
  const auto y3 = Sub<16>(m * Sub<35>(s, x3), 8_c * y.Squared().Squared());
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
