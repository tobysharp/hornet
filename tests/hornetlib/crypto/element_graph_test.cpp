// Copyright 2026 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

// Proof-of-concept tests for eager typed field values (element_graph.h).
//
// Acceptance criteria:
//  - Operators delegate to the eager FieldElement ops, so results are BIT-identical
//    (Words() equality), not just semantically equal mod p.
//  - Transcribing the full point.h arithmetic surface (jac/jac add, mixed add, affine
//    add/sub/double, unary negations, jac subtraction, AddWithZRatio, jacobian double) with
//    plain `auto` and zero per-op annotations re-derives every hand-written magnitude constant:
//    the Subtract<...> subtrahend bounds arise inside operator-, and the outputs pin the
//    closure constants per formula.
//  - Every named intermediate is computed exactly once, by construction (evaluation happens at
//    operator application, in declaration order -- the topological order of the dataflow DAG).
//  - Admission (m_a*m_b <= 8191, weak-normalize <= 4095, cap 4096) is compile-time; deleted
//    fallbacks stop inadmissible ops resolving to Value's inherited untyped operators.
//  - In magnitude-checked builds, an under-annotated Source is caught when the op evaluates.

#include <cstdint>
#include <random>
#include <tuple>

#include <gtest/gtest.h>

#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/element_graph.h"

namespace hornet::crypto::ecdsa {
namespace {

using graph::Source;
using graph::Value;
using graph::XCoord;
using graph::YCoord;
using graph::ZCoord;
using AffF = AffinePoint;
using JacF = JacobianPoint;

// Random element with words at the top of the given magnitude's range.
FieldElement RandomAt(std::mt19937_64& rng, int magnitude) {
  FieldElement::Array a;
  for (int i = 0; i < 4; ++i) a[i] = rng() % (uint64_t(magnitude) << 52);
  a[4] = rng() % (uint64_t(magnitude) << 48);
  a[0] |= 1;  // never the zero pattern
  return FieldElement{a, magnitude};
}

AffF RandomAffine(std::mt19937_64& rng) { return {RandomAt(rng, 2), RandomAt(rng, 3)}; }
JacF RandomJacobian(std::mt19937_64& rng) {
  return {RandomAt(rng, 40), RandomAt(rng, 19), RandomAt(rng, 8)};
}

void ExpectSameWords(const FieldElement& lhs, const FieldElement& rhs, const char* what) {
  EXPECT_EQ(lhs.Words(), rhs.Words()) << what;
}

void ExpectSameAffine(const AffF& actual, const AffF& expected, const char* what) {
  ExpectSameWords(actual.x, expected.x, what);
  ExpectSameWords(actual.y, expected.y, what);
}

void ExpectSameJacobian(const JacF& actual, const JacF& expected, const char* what) {
  ExpectSameWords(actual.X, expected.X, what);
  ExpectSameWords(actual.Y, expected.Y, what);
  ExpectSameWords(actual.Z, expected.Z, what);
}

// ---- Structure: sources are views, results are typed FieldElements, everything is constexpr ----

static_assert(sizeof(Source<2>) == sizeof(void*));

static_assert([] {
  const FieldElement two{2};
  const FieldElement five{5};
  return FieldElement{Source<1>{two} + Source<1>{five} * Source<1>{two}} == FieldElement{12};
}());

// ---- Magnitude calculus: operator results carry the derived bound ----

using S1 = Source<1>;
using S40 = Source<40>;
static_assert(std::same_as<decltype(std::declval<S1>() + std::declval<S40>()), Value<41>>);
static_assert(std::same_as<decltype(std::declval<S40>() - std::declval<S1>()), Value<42>>);
static_assert(std::same_as<decltype(-std::declval<S40>()), Value<41>>);
static_assert(std::same_as<decltype(std::declval<S40>() * std::declval<S40>()), Value<2>>);
static_assert(std::same_as<decltype(std::declval<S40>().Squared()), Value<2>>);
static_assert(std::same_as<decltype(std::declval<S40>().Times<3>()), Value<120>>);
static_assert(std::same_as<decltype(std::declval<S40>().NormalizeWeak()), Value<2>>);
static_assert(std::same_as<decltype(std::declval<S40>() / std::declval<S1>()), Value<1>>);

// ---- Admission is compile-time; deleted fallbacks block the inherited untyped operators ----

template <class A, class B> concept CanMul = requires(const A& a, const B& b) { a * b; };
template <class A, class B> concept CanAdd = requires(const A& a, const B& b) { a + b; };
template <class A, class B> concept CanDiv = requires(const A& a, const B& b) { a / b; };
template <class A> concept CanSquare = requires(const A& a) { a.Squared(); };
template <class A> concept CanWeak = requires(const A& a) { a.NormalizeWeak(); };
template <class A> concept CanNegate = requires(const A& a) { -a; };

static_assert(CanMul<Source<90>, Source<91>>);   // 8190 <= 8191
static_assert(!CanMul<Source<91>, Source<91>>);  // 8281 > 8191
static_assert(!CanMul<Value<91>, Value<91>>);    // deleted, NOT the inherited FieldElement op
static_assert(CanSquare<Source<90>>);            // 8100 <= 8191
static_assert(!CanSquare<Source<91>>);
static_assert(!CanSquare<Value<91>>);            // using-declaration hides the untyped Squared
static_assert(CanWeak<Source<4095>>);
static_assert(!CanWeak<Source<4096>>);
static_assert(CanAdd<Source<4000>, Source<96>>);  // 4096 cap
static_assert(!CanAdd<Source<4000>, Source<97>>);
static_assert(!CanNegate<Source<4096>>);
static_assert(CanDiv<Source<4095>, Source<4095>>);  // Pack requires weak-normalize admission
static_assert(!CanDiv<Source<4096>, Source<1>>);

TEST(ElementGraphTest, OpsMatchEagerOpsBitwise) {
  std::mt19937_64 rng{20260719};
  for (int trial = 0; trial < 200; ++trial) {
    const FieldElement a = RandomAt(rng, 40);
    const FieldElement b = RandomAt(rng, 19);
    ExpectSameWords(Source<40>{a} + Source<19>{b}, a + b, "add");
    ExpectSameWords(Source<40>{a} - Source<19>{b}, a.Subtract<19>(b), "sub");
    ExpectSameWords(-Source<19>{b}, b.Negate<19>(), "neg");
    ExpectSameWords(Source<40>{a} * Source<19>{b}, a * b, "mul");
    ExpectSameWords(Source<40>{a}.Squared(), a.Squared(), "square");
    ExpectSameWords(3_c * Source<40>{a}, a.Times<3>(), "times");
    ExpectSameWords(Source<40>{a}.NormalizeWeak(), a.NormalizeWeak(), "weak");
    ExpectSameWords(Source<40>{a} / Source<19>{b}, a / b, "div");
  }
}

// ---- Transcriptions of the point.h arithmetic surface. Entry bounds (the coordinate helpers)
// ---- are the only annotations; every Subtract constant point.h spells by hand is re-derived.

AffF NegAffineGraph(const AffF& p) { return {p.x, -YCoord(p)}; }

JacF NegJacobianGraph(const JacF& p) { return {p.X, -YCoord(p), p.Z}; }

// AffinePoint operator+.
AffF AffineAddGraph(const AffF& lhs, const AffF& rhs) {
  using namespace graph;
  if (lhs.IsInfinity()) return rhs;
  if (rhs.IsInfinity()) return lhs;
  if (lhs.x != rhs.x) {
    const auto lambda = (YCoord(rhs) - YCoord(lhs)) / (XCoord(rhs) - XCoord(lhs));
    const auto x3 = lambda.Squared() - XCoord(lhs) - XCoord(rhs);
    const auto y3 = lambda * (XCoord(lhs) - x3) - YCoord(lhs);
    static_assert(std::same_as<decltype(x3), const Value<8>>);
    static_assert(std::same_as<decltype(y3), const Value<6>>);
    return {x3.NormalizeWeak(), y3.NormalizeWeak()};
  } else if (lhs.y == FieldElement{-YCoord(rhs)}) return {};
  else return lhs.Double();
}

// AffinePoint operator-.
AffF AffineSubGraph(const AffF& lhs, const AffF& rhs) {
  using namespace graph;
  if (lhs.IsInfinity()) return NegAffineGraph(rhs);
  if (rhs.IsInfinity()) return lhs;
  if (lhs.x != rhs.x) {
    const auto lambda = (-YCoord(rhs) - YCoord(lhs)) / (XCoord(rhs) - XCoord(lhs));
    const auto x3 = lambda.Squared() - XCoord(lhs) - XCoord(rhs);
    const auto y3 = lambda * (XCoord(lhs) - x3) - YCoord(lhs);
    return {x3.NormalizeWeak(), y3.NormalizeWeak()};
  } else if (lhs.y == rhs.y) return {};
  else return lhs.Double();
}

// AffinePoint::Double.
AffF AffineDoubleGraph(const AffF& p) {
  using namespace graph;
  if (p.IsInfinity()) return {};
  const auto lambda = (3_c * XCoord(p).Squared()) / (YCoord(p) + YCoord(p));
  const auto x3 = lambda.Squared() - (XCoord(p) + XCoord(p));
  const auto y3 = lambda * (XCoord(p) - x3) - YCoord(p);
  static_assert(std::same_as<decltype(x3), const Value<7>>);
  static_assert(std::same_as<decltype(y3), const Value<6>>);
  return {x3.NormalizeWeak(), y3.NormalizeWeak()};
}

// JacobianPoint operator+(Affine, Jacobian), both branches.
JacF MixedAddGraph(const AffF& lhs, const JacF& rhs) {
  using namespace graph;
  if (lhs.IsInfinity()) return rhs;
  if (rhs.IsInfinity()) return lhs;

  // 3M, 1S
  const auto rZ2 = ZCoord(rhs).Squared();
  const auto U_1 = XCoord(lhs) * rZ2;
  const auto H = XCoord(rhs) - U_1;
  const auto S_1 = YCoord(lhs) * rZ2 * ZCoord(rhs);
  const auto r = YCoord(rhs) - S_1;

  if (H == 0) {
    if (r == 0) {
      // 1M, 5S doubling branch
      const auto X2 = XCoord(lhs).Squared();
      const auto M = 3_c * X2;
      const auto Y2 = YCoord(lhs).Squared();
      const auto Y4 = Y2.Squared();
      const auto S = 2_c * ((XCoord(lhs) + Y2).Squared() - X2 - Y4);
      const auto X_3 = M.Squared() - 2_c * S;
      const auto Y_3 = M * (S - X_3) - 8_c * Y4;
      const auto Z_3 = 2_c * YCoord(lhs);
      static_assert(std::same_as<decltype(X_3), const Value<35>>);
      static_assert(std::same_as<decltype(Y_3), const Value<19>>);
      static_assert(std::same_as<decltype(Z_3), const Value<6>>);
      return {X_3, Y_3, Z_3};
    } else return {};
  }

  // 4M, 3S
  const auto H2 = H.Squared();
  const auto H3 = H2 * H;
  const auto U_1H2 = U_1 * H2;
  const auto X_3 = 4_c * (r.Squared() - H3 - 2_c * U_1H2);
  const auto Y_3 = r * (8_c * U_1H2 - 2_c * X_3) - 8_c * (S_1 * H3);
  const auto Z_3 = (ZCoord(rhs) + H).Squared() - rZ2 - H2;
  static_assert(std::same_as<decltype(X_3), const Value<40>>);
  static_assert(std::same_as<decltype(Y_3), const Value<19>>);
  static_assert(std::same_as<decltype(Z_3), const Value<8>>);
  return {X_3, Y_3, Z_3};
}

// JacobianPoint operator+(Jacobian, Jacobian), both branches.
JacF JacobianAddGraph(const JacF& lhs, const JacF& rhs) {
  using namespace graph;
  if (lhs.IsInfinity()) return rhs;
  if (rhs.IsInfinity()) return lhs;

  // 6M, 2S
  const auto lZ2 = ZCoord(lhs).Squared();
  const auto rZ2 = ZCoord(rhs).Squared();
  const auto U_1 = XCoord(lhs) * rZ2;
  const auto U_2 = XCoord(rhs) * lZ2;
  const auto H = U_2 - U_1;
  const auto S_1 = YCoord(lhs) * rZ2 * ZCoord(rhs);
  const auto S_2 = YCoord(rhs) * lZ2 * ZCoord(lhs);
  const auto r = S_2 - S_1;

  if (H == 0) {
    if (r == 0) {
      // 2M, 5S doubling branch
      const auto X2 = XCoord(lhs).Squared();
      const auto M = 3_c * X2;
      const auto Y2 = YCoord(lhs).Squared();
      const auto S = 4_c * XCoord(lhs) * Y2;
      const auto X_3 = M.Squared() - 2_c * S;
      const auto Y_3 = M * (S - X_3) - 8_c * Y2.Squared();
      const auto Z_3 = (YCoord(lhs) + ZCoord(lhs)).Squared() - Y2 - lZ2;
      static_assert(std::same_as<decltype(X_3), const Value<7>>);
      static_assert(std::same_as<decltype(Y_3), const Value<19>>);
      static_assert(std::same_as<decltype(Z_3), const Value<8>>);
      return {X_3, Y_3, Z_3};
    } else return {};
  }

  // 5M, 3S
  const auto H2 = H.Squared();
  const auto H3 = H2 * H;
  const auto U_1H2 = U_1 * H2;
  const auto X_3 = 4_c * (r.Squared() - H3 - 2_c * U_1H2);
  const auto Y_3 = r * (8_c * U_1H2 - 2_c * X_3) - 8_c * (S_1 * H3);
  const auto Z_3 = H * ((ZCoord(lhs) + ZCoord(rhs)).Squared() - lZ2 - rZ2);
  static_assert(std::same_as<decltype(X_3), const Value<40>>);
  static_assert(std::same_as<decltype(Y_3), const Value<19>>);
  static_assert(std::same_as<decltype(Z_3), const Value<2>>);
  return {X_3, Y_3, Z_3};
}

// JacobianPoint operator-(Jacobian, Jacobian) == lhs + (-rhs): the negation flows through the
// magnitude calculus (YCoord 19 -> Value<20> after unary minus), no wider entry bound needed.
JacF JacobianSubGraph(const JacF& lhs, const JacF& rhs) {
  using namespace graph;
  if (lhs.IsInfinity()) return NegJacobianGraph(rhs);
  if (rhs.IsInfinity()) return lhs;

  const auto lZ2 = ZCoord(lhs).Squared();
  const auto rZ2 = ZCoord(rhs).Squared();
  const auto U_1 = XCoord(lhs) * rZ2;
  const auto U_2 = XCoord(rhs) * lZ2;
  const auto H = U_2 - U_1;
  const auto S_1 = YCoord(lhs) * rZ2 * ZCoord(rhs);
  const auto S_2 = (-YCoord(rhs)) * lZ2 * ZCoord(lhs);
  const auto r = S_2 - S_1;

  if (H == 0) {
    if (r == 0) {
      const auto X2 = XCoord(lhs).Squared();
      const auto M = 3_c * X2;
      const auto Y2 = YCoord(lhs).Squared();
      const auto S = 4_c * XCoord(lhs) * Y2;
      const auto X_3 = M.Squared() - 2_c * S;
      const auto Y_3 = M * (S - X_3) - 8_c * Y2.Squared();
      const auto Z_3 = (YCoord(lhs) + ZCoord(lhs)).Squared() - Y2 - lZ2;
      return {X_3, Y_3, Z_3};
    } else return {};
  }

  const auto H2 = H.Squared();
  const auto H3 = H2 * H;
  const auto U_1H2 = U_1 * H2;
  const auto X_3 = 4_c * (r.Squared() - H3 - 2_c * U_1H2);
  const auto Y_3 = r * (8_c * U_1H2 - 2_c * X_3) - 8_c * (S_1 * H3);
  const auto Z_3 = H * ((ZCoord(lhs) + ZCoord(rhs)).Squared() - lZ2 - rZ2);
  return {X_3, Y_3, Z_3};
}

// JacobianPoint::Double.
JacF DoubleGraph(const JacF& p) {
  using namespace graph;
  if (p.IsInfinity()) return {};
  const auto X = XCoord(p);
  const auto Y = YCoord(p);
  const auto Z = ZCoord(p);
  const auto X2 = X.Squared();
  const auto Z2 = Z.Squared();
  const auto M = 3_c * X2;
  const auto Y2 = Y.Squared();
  const auto Y4 = Y2.Squared();
  const auto S = 2_c * ((X + Y2).Squared() - X2 - Y4);
  const auto X_3 = M.Squared() - 2_c * S;
  const auto Y_3 = M * (S - X_3) - 8_c * Y4;
  const auto Z_3 = (Y + Z).Squared() - Y2 - Z2;
  static_assert(std::same_as<decltype(S), const Value<16>>);
  static_assert(std::same_as<decltype(X_3), const Value<35>>);
  static_assert(std::same_as<decltype(Y_3), const Value<19>>);
  static_assert(std::same_as<decltype(Z_3), const Value<8>>);
  return {X_3, Y_3, Z_3};
}

// JacobianPoint::AddWithZRatio. The (Z + H)^2 square is the worst admission in the point layer
// (51^2 = 2601 of 8191), and the z-ratio 2H carries the widest bound (86) -- both derived here.
std::tuple<JacF, FieldElement> AddWithZRatioGraph(const JacF& jac, const AffF& affine) {
  using namespace graph;
  const auto rZ2 = ZCoord(jac).Squared();
  const auto U_1 = XCoord(affine) * rZ2;
  const auto H = XCoord(jac) - U_1;
  const auto S_1 = YCoord(affine) * rZ2 * ZCoord(jac);
  const auto r = YCoord(jac) - S_1;

  const auto H2 = H.Squared();
  const auto H3 = H2 * H;
  const auto U_1H2 = U_1 * H2;
  const auto X_3 = 4_c * (r.Squared() - H3 - 2_c * U_1H2);
  const auto Y_3 = r * (8_c * U_1H2 - 2_c * X_3) - 8_c * (S_1 * H3);
  const auto Z_3 = (ZCoord(jac) + H).Squared() - rZ2 - H2;
  const auto ratio = 2_c * H;
  static_assert(std::same_as<decltype(H), const Value<43>>);
  static_assert(std::same_as<decltype(ratio), const Value<86>>);
  static_assert(std::same_as<decltype(X_3), const Value<40>>);
  static_assert(std::same_as<decltype(Y_3), const Value<19>>);
  static_assert(std::same_as<decltype(Z_3), const Value<8>>);
  return {{X_3, Y_3, Z_3}, ratio};
}

// ---- Differentials: every transcription, bit-identical to point.h on every branch ----

TEST(ElementGraphTest, UnaryNegationsMatchBitwise) {
  std::mt19937_64 rng{90921};
  for (int trial = 0; trial < 50; ++trial) {
    const AffF a = RandomAffine(rng);
    const JacF j = RandomJacobian(rng);
    ExpectSameAffine(NegAffineGraph(a), -a, "-affine");
    ExpectSameJacobian(NegJacobianGraph(j), -j, "-jacobian");
  }
}

TEST(ElementGraphTest, AffineAddMatchesBitwise) {
  std::mt19937_64 rng{90922};
  for (int trial = 0; trial < 50; ++trial) {
    const AffF a = RandomAffine(rng);
    const AffF b = RandomAffine(rng);
    ExpectSameAffine(AffineAddGraph(a, b), a + b, "general");
    ExpectSameAffine(AffineAddGraph(a, a), a + a, "doubling fallthrough");
    ExpectSameAffine(AffineAddGraph(a, AffF{}), a + AffF{}, "rhs infinity");
    // Inverse point (same x, negated y): must yield infinity. Weak-normalize keeps the
    // constructed y within the affine storage bound.
    const AffF neg{a.x, FieldElement{(-YCoord(a)).NormalizeWeak()}};
    const AffF sum = AffineAddGraph(a, neg);
    EXPECT_TRUE(sum.IsInfinity());
    EXPECT_TRUE((a + neg).IsInfinity());
  }
}

TEST(ElementGraphTest, AffineSubMatchesBitwise) {
  std::mt19937_64 rng{90923};
  for (int trial = 0; trial < 50; ++trial) {
    const AffF a = RandomAffine(rng);
    const AffF b = RandomAffine(rng);
    ExpectSameAffine(AffineSubGraph(a, b), a - b, "general");
    EXPECT_TRUE(AffineSubGraph(a, a).IsInfinity());
    EXPECT_TRUE((a - a).IsInfinity());
    ExpectSameAffine(AffineSubGraph(AffF{}, b), AffF{} - b, "lhs infinity -> -rhs");
  }
}

TEST(ElementGraphTest, AffineDoubleMatchesBitwise) {
  std::mt19937_64 rng{90924};
  for (int trial = 0; trial < 50; ++trial) {
    const AffF a = RandomAffine(rng);
    ExpectSameAffine(AffineDoubleGraph(a), a.Double(), "affine double");
  }
}

TEST(ElementGraphTest, MixedAddMatchesBitwise) {
  std::mt19937_64 rng{90917};
  for (int trial = 0; trial < 50; ++trial) {
    const AffF lhs = RandomAffine(rng);
    const JacF rhs = RandomJacobian(rng);
    ExpectSameJacobian(MixedAddGraph(lhs, rhs), lhs + rhs, "general");

    // Same point as a Z=1 Jacobian: the doubling branch.
    const JacF same{lhs.x, lhs.y, FieldElement{1}};
    ExpectSameJacobian(MixedAddGraph(lhs, same), lhs + same, "doubling branch");

    // Inverse point: infinity.
    const JacF neg{lhs.x, FieldElement{(-YCoord(lhs)).NormalizeWeak()}, FieldElement{1}};
    EXPECT_TRUE(MixedAddGraph(lhs, neg).IsInfinity());
    EXPECT_TRUE((lhs + neg).IsInfinity());
  }
}

TEST(ElementGraphTest, JacobianAddMatchesBitwise) {
  std::mt19937_64 rng{90925};
  for (int trial = 0; trial < 50; ++trial) {
    const JacF lhs = RandomJacobian(rng);
    const JacF rhs = RandomJacobian(rng);
    ExpectSameJacobian(JacobianAddGraph(lhs, rhs), lhs + rhs, "general");

    // The same point under a rescaled representation ((t^2)X, (t^3)Y, tZ): doubling branch.
    const FieldElement t = RandomAt(rng, 1);
    const FieldElement t2 = t.Squared();
    const JacF rescaled{lhs.X * t2, lhs.Y * t2 * t, lhs.Z * t};
    ExpectSameJacobian(JacobianAddGraph(lhs, rescaled), lhs + rescaled, "doubling branch");
    ExpectSameJacobian(JacobianAddGraph(lhs, lhs), lhs + lhs, "doubling, identical rep");
  }
}

TEST(ElementGraphTest, JacobianSubMatchesBitwise) {
  std::mt19937_64 rng{90926};
  for (int trial = 0; trial < 50; ++trial) {
    const JacF lhs = RandomJacobian(rng);
    const JacF rhs = RandomJacobian(rng);
    ExpectSameJacobian(JacobianSubGraph(lhs, rhs), lhs - rhs, "general");
    EXPECT_TRUE(JacobianSubGraph(lhs, lhs).IsInfinity());  // P - P
    EXPECT_TRUE((lhs - lhs).IsInfinity());
  }
}

TEST(ElementGraphTest, DoubleMatchesBitwise) {
  std::mt19937_64 rng{90918};
  for (int trial = 0; trial < 50; ++trial) {
    const JacF p = RandomJacobian(rng);
    ExpectSameJacobian(DoubleGraph(p), p.Double(), "jacobian double");
  }
}

TEST(ElementGraphTest, AddWithZRatioMatchesBitwise) {
  std::mt19937_64 rng{90927};
  for (int trial = 0; trial < 50; ++trial) {
    const JacF jac = RandomJacobian(rng);
    const AffF affine = RandomAffine(rng);
    const auto [point, ratio] = AddWithZRatioGraph(jac, affine);
    const auto [expected_point, expected_ratio] = jac.AddWithZRatio(affine);
    ExpectSameJacobian(point, expected_point, "point");
    ExpectSameWords(ratio, expected_ratio, "z-ratio");
  }
}

// ---- Spelling and semantics ----

TEST(ElementGraphTest, SketchSpellingComposes) {
  // The target spelling: bare FieldElements mix in at the default entry bound (2), Source{}
  // wraps explicitly, and every named intermediate is a computed Value usable as a FieldElement.
  std::mt19937_64 rng{90919};
  const AffF lhs{RandomAt(rng, 2), RandomAt(rng, 2)};
  const JacF rhs{RandomAt(rng, 2), RandomAt(rng, 2), RandomAt(rng, 2)};

  const auto rZ2 = Source{rhs.Z}.Squared();
  const auto U_1 = lhs.x * rZ2;
  const auto& U_2 = rhs.X;
  const auto H = U_2 - U_1;
  const auto S_1 = lhs.y * rZ2 * rhs.Z;
  const auto& S_2 = rhs.Y;
  const auto r = S_2 - S_1;

  const FieldElement h = H;  // Value IS-A FieldElement
  ExpectSameWords(h, rhs.X.Subtract<2>(lhs.x * rhs.Z.Squared()), "H");
  ExpectSameWords(r, rhs.Y.Subtract<2>(lhs.y * rhs.Z.Squared() * rhs.Z), "r");
}

TEST(ElementGraphTest, ReusedValueIsComputedOnce) {
  // sq is evaluated at its declaration; both uses below read the stored result. (Contrast the
  // abandoned lazy-recipe design, where this spelling re-squared per use: 26 vs 8 muls on
  // Double.)
  std::mt19937_64 rng{90920};
  const FieldElement a = RandomAt(rng, 3);
  const auto sq = Source<3>{a}.Squared();
  static_assert(std::same_as<decltype(sq), const Value<2>>);
  ExpectSameWords(sq * sq, a.Squared() * a.Squared(), "reuse");
  ExpectSameWords(sq.Squared(), a.Squared().Squared(), "chained");
}

TEST(ElementGraphTest, CheckedBuildCatchesUnderAnnotatedSource) {
  if constexpr (kCheckMagnitudes) {
    // Claimed bound 1, actual magnitude 3: negation picks the too-small (m+1)p constant; the
    // element's runtime check must reject it when the op evaluates.
    FieldElement::Array a{};
    a[0] = (uint64_t{3} << 52) - 1;
    const FieldElement e{a, 3};
    EXPECT_THROW((void)(-Source<1>{e}), std::out_of_range);
  } else {
    GTEST_SKIP() << "magnitude checking disabled in this build";
  }
}

}  // namespace
}  // namespace hornet::crypto::ecdsa
