#pragma once

#include <iterator>
#include <utility>

#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/naf.h"
#include "hornetlib/crypto/secp256k1.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/assert.h"

namespace hornet::crypto::ecdsa {

// Signed scalar: unsigned half-width magnitude + sign (folded into the wNAF digits at recoding).
struct SignedScalar {
  UIntW<128> magnitude;
  bool negative;
};

// k1 + k2*lambda == k (mod n), with |k1|, |k2| ~ sqrt(n).
struct LambdaSplit {
  SignedScalar k1;  // coefficient of the base point P
  SignedScalar k2;  // coefficient of phi(P) = lambda*P
};

// A GLV term: a scalar's lambda split with the odd-multiple tables of its base P and of phi(P).
// Table is the table storage -- a span viewing fixed tables (G) or an owning array (per-call, Q).
// LinearCombination_GLV sums two of these -- the G-side and the Q-side.
template <class Table>
struct GlvTerm {
  using Mod_p = Fp<secp256k1::kBits, secp256k1::p>;

  LambdaSplit scalar;
  Table base;  // odd multiples of P
  Table phi;   // odd multiples of phi(P)
  Mod_p global_z = 1;

  // wNAF recodings of the split scalars (k1 for base, k2 for phi), sign folded in; width is
  // inferred from the table size (2^{w-1} entries).
  auto NonAdjacentFormDigits() const {
    const int width = std::bit_width(base.size());
    return std::pair{WindowedNonAdjacentForm(scalar.k1.magnitude, width, scalar.k1.negative),
                     WindowedNonAdjacentForm(scalar.k2.magnitude, width, scalar.k2.negative)};
  }

  // The base / phi table entry indexed by a nonzero, sign-folded wNAF digit.
  const auto& Base(int digit) const { return base[(digit + std::ssize(base) - 1) >> 1]; }
  const auto& Phi(int digit) const { return phi[(digit + std::ssize(phi) - 1) >> 1]; }
};

namespace detail {

// Fixed-point reciprocal round(2^384 * g / n) for the multiply-shift Babai quotient below.
inline consteval UInt256 GlvReciprocal(const UInt256& g) {
  const auto rounded = (g.ZeroExtend<512>() << 384) + (secp256k1::n >> 1);
  const auto quotient = rounded.QuotientRemainder(secp256k1::n).first;
  Assert(quotient.HighBits<256>() == 0);
  return quotient.LowBits<256>();
}

inline constexpr UInt256 kReciprocalB2 = GlvReciprocal(secp256k1::glv_b2);
inline constexpr UInt256 kReciprocalMinusB1 = GlvReciprocal(secp256k1::glv_minus_b1);

// round(g*k/n) ~= (g_hat*k + 2^383) >> 384, g_hat = GlvReciprocal(g).
inline constexpr UInt256 RoundDivide(const UInt256& g_hat, const UInt256& k) {
  constexpr auto kHalf = UIntW<512>{1} << 383;
  return ((g_hat.MultiplyWide(k) + kHalf) >> 384).LowBits<256>();
}

}  // namespace detail

// Decomposes k (reduced mod n) as k == k1 + k2*lambda (mod n) with |k1|, |k2| ~ sqrt(n), by Babai
// rounding on the lattice basis (a1,b1),(a2,b2), a1*b2-a2*b1=n, b1<0 passed as minus_b1=-b1:
//   beta1~=round(b2*k/n), beta2~=round(-b1*k/n); k1 = k-beta1*a1-beta2*a2; k2 = -(beta1*b1+beta2*b2).
inline LambdaSplit SplitLambda(const UInt256& k) {
  using namespace secp256k1;
  using Mod_n = Fp<kBits, n>;
  Assert(k < n);

  // Note that RoundDivide approximates round(b*k/n) closely enough that we still easily guarantee |k_i| < 2^128.
  const Mod_n beta1 = detail::RoundDivide(detail::kReciprocalB2, k);
  const Mod_n beta2 = detail::RoundDivide(detail::kReciprocalMinusB1, k);

  // k2 = minus_b1*beta1 - b2*beta2;  k1 = k - a1*beta1 - a2*beta2  (mod n, as Fp(n)).
  const Mod_n k2 = Mod_n{glv_minus_b1} * beta1 - Mod_n{glv_b2} * beta2;
  const Mod_n k1 = Mod_n{k} - Mod_n{glv_a1} * beta1 - Mod_n{glv_a2} * beta2;

  // Nearest signed representative: |k_i| << n/2, so a residue above n/2 is negative (magnitude n-x).
  const UInt256 half = n >> 1;
  const auto to_signed = [&half](const Mod_n& v) -> SignedScalar {
    const bool negative = v.x > half;
    const UInt256 magnitude = negative ? n - v.x : v.x;
    Assert(magnitude.HighBits<128>() == 0);
    return {magnitude.LowBits<128>(), negative};
  };
  return {to_signed(k1), to_signed(k2)};
}

// Builds an owning Q-side GlvTerm: the odd-multiple tables of Q and phi(Q) (2^{kWidth-1} entries
// each) packed with the scalar split. The term owns its tables, so no external storage is needed.
template <int kWidth = 5>
GlvTerm<std::array<AffinePoint, 1 << (kWidth - 1)>> MakeVariableGlvTerm(
    const LambdaSplit& scalar, const AffinePoint& Q) {
  GlvTerm<std::array<AffinePoint, 1 << (kWidth - 1)>> term;
  term.scalar = scalar;
  term.global_z = PrecomputeTableGlobalZ(Q, {term.base.data(), term.base.size()});
  for (std::size_t i = 0; i < term.base.size(); ++i)
    term.phi[i] = { secp256k1::beta * term.base[i].x, term.base[i].y };
  return term;
}

}  // namespace hornet::crypto::ecdsa
