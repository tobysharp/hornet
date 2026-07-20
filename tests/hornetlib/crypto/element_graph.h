// Copyright 2026 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

// Statically-bounded field arithmetic: magnitude bounds ride the types.
//
// Every operator evaluates at construction and returns a Value<M> holding the result, with M
// derived from the operand bounds -- C++ declaration order is a topological order of the
// dataflow DAG, so each named intermediate is computed exactly once and read by reference.
// Source<M>{fe} is an 8-byte view annotating an entry bound, the only annotations anywhere;
// a bare FieldElement in a mixed expression is assumed at kProductMag. Negation takes its
// (m+1)p constant from the operand's static bound, and admission (m_a m_b <= 8191,
// weak-normalize <= 4095, magnitude cap 4096) is enforced by constraints -- each operator has
// an unconditional deleted twin that subsumption demotes to, so an inadmissible op is a hard
// error rather than silently resolving to the untyped FieldElement operators Value inherits.

#pragma once

#include <type_traits>

#include "hornetlib/crypto/element.h"
#include "hornetlib/crypto/point.h"

namespace hornet::crypto::ecdsa::graph {

inline constexpr int kProductMag = 2;  // any mul/square output; also the bare-operand default

struct NodeBase {};
template <class T> concept IsNode = std::derived_from<std::remove_cvref_t<T>, NodeBase>;
template <class T> concept IsOperand = IsNode<T> || std::same_as<std::remove_cvref_t<T>, FieldElement>;
template <class L, class R> concept Operands = IsOperand<L> && IsOperand<R> && (IsNode<L> || IsNode<R>);

template <IsOperand T> consteval int MagOf() {
  if constexpr (IsNode<T>) return std::remove_cvref_t<T>::kMag;
  else return kProductMag;
}

template <int M> struct Value;

template <class Derived>
struct Expression : NodeBase {
  constexpr const Derived& Self() const { return static_cast<const Derived&>(*this); }

  constexpr auto Squared() const
      requires (Derived::kMag * Derived::kMag <= FieldElement::kMaxProductMagnitude) {
    return Value<2>{[&] { return Fe(Self()).Squared(); }};
  }

  template <int k> constexpr auto Times() const
      requires (k >= 1 && k * Derived::kMag <= FieldElement::kMaxMagnitude) {
    return Value<k * Derived::kMag>{[&] { return Fe(Self()).template Times<k>(); }};
  }

  constexpr auto NormalizeWeak() const requires (Derived::kMag <= FieldElement::kMaxMagnitude - 1) {
    return Value<2>{[&] { return Fe(Self()).NormalizeWeak(); }};
  }

  constexpr auto Half() const requires (Derived::kMag <= FieldElement::kMaxMagnitude - 1) {
    return Value<((Derived::kMag + 1) >> 1) + 1>{[&] { return Fe(Self()).Half(); }};
  }
};

template <int M = kProductMag>
struct Source : Expression<Source<M>> {
  static constexpr int kMag = M;
  constexpr Source(const FieldElement& f) : fe(&f) {}
  const FieldElement* fe;
};

// The FieldElement an operand denotes: a Source dereferences; Value and FieldElement bind
// directly (Value IS-A FieldElement).
constexpr const FieldElement& Fe(const FieldElement& f) { return f; }
template <int M> constexpr const FieldElement& Fe(const Source<M>& s) { return *s.fe; }

// An operator result: a FieldElement whose magnitude bound rides the type. The base is
// initialized from the compute callable's prvalue, so the result lands directly in the object
// (a FieldElement parameter would cost a 40-byte copy per op -- measured).
template <int M>
struct Value : FieldElement, Expression<Value<M>> {
  static constexpr int kMag = M;
  template <class F>
    requires std::same_as<std::invoke_result_t<F&>, FieldElement>
  constexpr explicit Value(F&& compute) : FieldElement(compute()) {}
  // Graph ops shadow the eager FieldElement members of the same name.
  using Expression<Value<M>>::Squared;
  using Expression<Value<M>>::Times;
  using Expression<Value<M>>::NormalizeWeak;
  using Expression<Value<M>>::Half;
};

template <class L, class R>
  requires (Operands<L, R> && MagOf<L>() + MagOf<R>() <= FieldElement::kMaxMagnitude)
constexpr auto operator+(const L& l, const R& r) {
  return Value<MagOf<L>() + MagOf<R>()>{[&] { return Fe(l) + Fe(r); }};
}
template <class L, class R> requires Operands<L, R> constexpr auto operator+(const L& l, const R& r) = delete;

template <class L, class R>
  requires (Operands<L, R> && MagOf<L>() + MagOf<R>() + 1 <= FieldElement::kMaxMagnitude)
constexpr auto operator-(const L& l, const R& r) {
  return Value<MagOf<L>() + MagOf<R>() + 1>{[&] { return Fe(l).template Subtract<MagOf<R>()>(Fe(r)); }};
}
template <class L, class R> requires Operands<L, R> constexpr auto operator-(const L& l, const R& r) = delete;

template <IsNode N>
  requires (MagOf<N>() + 1 <= FieldElement::kMaxMagnitude)
constexpr auto operator-(const N& n) {
  return Value<MagOf<N>() + 1>{[&] { return Fe(n).template Negate<MagOf<N>()>(); }};
}
template <IsNode N> constexpr auto operator-(const N& n) = delete;

template <class L, class R>
  requires (Operands<L, R> && MagOf<L>() * MagOf<R>() <= FieldElement::kMaxProductMagnitude)
constexpr auto operator*(const L& l, const R& r) {
  return Value<2>{[&] { return Fe(l) * Fe(r); }};
}
template <class L, class R> requires Operands<L, R> constexpr auto operator*(const L& l, const R& r) = delete;

// Division packs both operands (a Normalize, requiring weak-normalize admissibility) and
// returns a canonical element.
template <class L, class R>
  requires (Operands<L, R> && MagOf<L>() <= FieldElement::kMaxMagnitude - 1 &&
            MagOf<R>() <= FieldElement::kMaxMagnitude - 1)
constexpr auto operator/(const L& l, const R& r) {
  return Value<1>{[&] { return Fe(l) / Fe(r); }};
}
template <class L, class R> requires Operands<L, R> constexpr auto operator/(const L& l, const R& r) = delete;

template <int k, IsNode N> constexpr auto operator*(std::integral_constant<int, k>, const N& n) {
  return n.template Times<k>();
}

// Storage-contract coordinate bounds: the least fixed point of the point.h formula suite over
// its own outputs, spelled as the shape of the formula that binds each one. The transcription
// tests pin these against the real formulas with independent numeric literals.
// Affine coordinates are weak-normalized on output; tables additionally store negated y.
inline constexpr int kAffineXMag = kProductMag;                                     // 2
inline constexpr int kAffineYMag = kProductMag + 1;                                 // 3
// Jacobian X binds at the add tails: X_3 = 4((r^2 - H^3) - 2 U_1 H^2).
inline constexpr int kJacobianXMag = 4 * ((kProductMag + kProductMag + 1) + 2 * kProductMag + 1);  // 40
// Jacobian Y binds at the add tails: Y_3 = r(...) - 8 S_1 H^3.
inline constexpr int kJacobianYMag = kProductMag + 8 * kProductMag + 1;             // 19
// Jacobian Z binds at the square-difference form: Z_3 = (a + b)^2 - a^2 - b^2.
inline constexpr int kJacobianZMag = (kProductMag + kProductMag + 1) + kProductMag + 1;  // 8

// Entry views for point coordinates at their storage bounds, so formulas spell no literals.
constexpr Source<kAffineXMag> XCoord(const AffinePoint& p) { return {p.x}; }
constexpr Source<kAffineYMag> YCoord(const AffinePoint& p) { return {p.y}; }
constexpr Source<kJacobianXMag> XCoord(const JacobianPoint& p) { return {p.X}; }
constexpr Source<kJacobianYMag> YCoord(const JacobianPoint& p) { return {p.Y}; }
constexpr Source<kJacobianZMag> ZCoord(const JacobianPoint& p) { return {p.Z}; }

}  // namespace hornet::crypto::ecdsa::graph
