#pragma once

#include <iterator>
#include <utility>

#include "hornetlib/crypto/fp.h"
#include "hornetlib/crypto/naf.h"
#include "hornetlib/crypto/uintw.h"
#include "hornetlib/util/assert.h"

namespace hornet::crypto::ecdsa {

// Signed scalar: unsigned magnitude + sign (folded into the wNAF digits at recoding).
template <int kBits>
struct SignedScalar {
  UIntW<kBits> magnitude;
  bool negative;
};

// k1 + k2*lambda == k (mod n), with |k1|, |k2| ~ sqrt(n).
template <int kBits>
struct LambdaSplit {
  SignedScalar<kBits> k1;  // coefficient of the base point P
  SignedScalar<kBits> k2;  // coefficient of phi(P) = lambda*P
};

// A GLV term: a scalar's lambda split with the odd-multiple tables of its base P and of phi(P).
// Table is the table storage -- a span viewing fixed tables (G) or an owning array (per-call, Q).
// LinearCombination_GLV sums two of these -- the G-side and the Q-side.
template <int kBits, class Table>
struct GlvTerm {
  LambdaSplit<kBits> scalar;
  Table base;  // odd multiples of P
  Table phi;   // odd multiples of phi(P)

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

// Decomposes k (reduced mod n) as k == k1 + k2*lambda (mod n) with |k1|, |k2| ~ sqrt(n), by Babai
// rounding on the lattice basis (a1,b1),(a2,b2), a1*b2-a2*b1=n, b1<0 passed as minus_b1=-b1:
//   beta1=round(b2*k/n), beta2=round(-b1*k/n); k1 = k-beta1*a1-beta2*a2; k2 = -(beta1*b1+beta2*b2).
template <int kBits, const UIntW<kBits>& n, const UIntW<kBits>& a1, const UIntW<kBits>& minus_b1,
          const UIntW<kBits>& a2, const UIntW<kBits>& b2>
LambdaSplit<kBits> SplitLambda(const UIntW<kBits>& k) {
  using Mod_n = Fp<kBits, n>;
  Assert(k < n);

  // round(num/n) = floor((num + n/2)/n), n odd. A quotient, not a residue -> raw division, not Fp.
  const auto round_div = [](const UIntW<2 * kBits>& num) -> UIntW<kBits> {
    const auto shifted = num + (n >> 1).template ZeroExtend<2 * kBits>();
    return shifted.QuotientRemainder(n).first.template LowBits<kBits>();
  };
  const Mod_n beta1{round_div(b2.MultiplyWide(k))};
  const Mod_n beta2{round_div(minus_b1.MultiplyWide(k))};

  // k2 = minus_b1*beta1 - b2*beta2;  k1 = k - a1*beta1 - a2*beta2  (mod n, as Fp(n)).
  const Mod_n k2 = Mod_n{minus_b1} * beta1 - Mod_n{b2} * beta2;
  const Mod_n k1 = Mod_n{k} - Mod_n{a1} * beta1 - Mod_n{a2} * beta2;

  // Nearest signed representative: |k_i| << n/2, so a residue above n/2 is negative (magnitude n-x).
  const UIntW<kBits> half = n >> 1;
  const auto to_signed = [&half](const Mod_n& v) -> SignedScalar<kBits> {
    return v.x > half ? SignedScalar<kBits>{n - v.x, true} : SignedScalar<kBits>{v.x, false};
  };
  return {to_signed(k1), to_signed(k2)};
}

// Builds an owning Q-side GlvTerm: the odd-multiple tables of Q and phi(Q) (2^{kWidth-1} entries
// each) packed with the scalar split. The term owns its tables, so no external storage is needed.
template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a, int kWidth = 5>
GlvTerm<kBits, std::array<JacobianPoint<kBits, p, a>, 1 << (kWidth - 1)>> MakeVariableGlvTerm(
    const LambdaSplit<kBits>& scalar, const AffinePoint<kBits, p, a>& Q, const Fp<kBits, p>& beta) {
  GlvTerm<kBits, std::array<JacobianPoint<kBits, p, a>, 1 << (kWidth - 1)>> term;
  term.scalar = scalar;
  PrecomputeTableJacobian(Q, {term.base.data(), term.base.size()});
  for (std::size_t i = 0; i < term.base.size(); ++i)
    term.phi[i] = {beta * term.base[i].X, term.base[i].Y, term.base[i].Z};
  return term;
}

}  // namespace hornet::crypto::ecdsa
