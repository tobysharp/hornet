#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <iterator>
#include <span>

#include "hornetlib/crypto/glv.h"
#include "hornetlib/crypto/naf.h"
#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

template <class Point>
constexpr Point Scale(const UInt256& scalar, const Point& pt) {
  const auto naf = NonAdjacentForm(scalar);
  constexpr int kBitCount = std::ssize(naf);

  Point sum;
  Point power = pt;
  int bitIndex = 0;
  for (; bitIndex < kBitCount && naf[bitIndex] == 0; ++bitIndex) {
    power = power.Double();
  }
  if (bitIndex < kBitCount) {
    sum = naf[bitIndex] > 0 ? power : -power;
    for (++bitIndex; bitIndex < kBitCount; ++bitIndex) {
      power = power.Double();
      if (naf[bitIndex] == 1) sum += power;
      else if (naf[bitIndex] == -1) sum -= power;
    }
  }
  return sum;
}

template <typename Element>
constexpr JacobianPoint<Element> LinearCombination(const UInt256& u1, const AffinePoint<Element>& P, const UInt256& u2,
                                          const AffinePoint<Element>& Q) {
  using Affine = AffinePoint<Element>;
  using Point = JacobianPoint<Element>;
  const auto naf1 = NonAdjacentForm(u1);
  const auto naf2 = NonAdjacentForm(u2);
  constexpr int kBitCount = std::ssize(naf1);

  const Point P_plus_Q = Point{P} + Q;
  const Point P_minus_Q = Point{P} - Q;
  const int add_kind[9] = {2, 1, 2, 1, 0, 1, 2, 1, 2};
  const Affine affine_addends[4] = {-P, -Q, Q, P};
  const Point jacobian_addends[5] = {-P_plus_Q, -P_minus_Q, {}, P_minus_Q, P_plus_Q};

  Point sum;
  for (int bitIndex = kBitCount - 1; bitIndex >= 0; --bitIndex) {
    sum = sum.Double();
    const int digit1 = naf1[bitIndex];
    const int digit2 = naf2[bitIndex];
    const int index = 3 * (digit1 + 1) + (digit2 + 1);
    if (add_kind[index] == 1) sum += affine_addends[index >> 1];
    else if (add_kind[index] == 2) sum += jacobian_addends[index >> 1];
  }
  return sum;
}

template <typename Element>
constexpr JacobianPoint<Element> LinearCombination_NAF_Disjoint(const UInt256& u1, const AffinePoint<Element>& P, const UInt256& u2,
                                                       const AffinePoint<Element>& Q) {
  using Affine = AffinePoint<Element>;
  using Point = JacobianPoint<Element>;
  const auto nafP = NonAdjacentForm(u1);
  const auto nafQ = NonAdjacentForm(u2);
  constexpr int kBitCount = std::ssize(nafP);

  const Affine P_addends[2] = {-P, P};
  const Affine Q_addends[2] = {-Q, Q};

  Point sum;
  for (int bitIndex = kBitCount - 1; bitIndex >= 0; --bitIndex) {
    sum = sum.Double();
    const int digitP = nafP[bitIndex];
    const int digitQ = nafQ[bitIndex];
    if (digitP != 0) sum += P_addends[(digitP + 1) >> 1];
    if (digitQ != 0) sum += Q_addends[(digitQ + 1) >> 1];
  }
  return sum;
}

// Computes u1*P + u2*Q with wNAF recoding. The fixed-base table P_table holds the odd
// affine multiples of P (built once via PrecomputeTableAffine); its width is inferred from
// the table size (2^{w-1} entries). The variable base Q gets a narrow per-call table.
template <typename Element>
inline JacobianPoint<Element> LinearCombination_wNAF(const UInt256& u1, std::span<const AffinePoint<Element>> P_table, const UInt256& u2,
                                            const AffinePoint<Element>& Q) {
  constexpr int kQWidth = 5;
  const int kPWidth = std::bit_width(P_table.size());  // 2^{w-1} entries -> window width w
  const auto nafP = WindowedNonAdjacentForm(u1, kPWidth);
  const auto nafQ = WindowedNonAdjacentForm(u2, kQWidth);
  constexpr int kBitCount = std::ssize(nafP);

  std::array<AffinePoint<Element>, 1 << (kQWidth - 1)> Q_table;
  const auto z = PrecomputeTableGlobalZ(Q, {Q_table.data(), Q_table.size()});
  const auto z2 = z.Squared();
  const auto z3 = z2 * z;
  const auto scaled = [&](const AffinePoint<Element>& pt) -> AffinePoint<Element> { return { pt.x * z2, pt.y * z3 }; };

  JacobianPoint<Element> sum;
  const int kPOffset = std::ssize(P_table) - 1;
  constexpr int kQOffset = (1 << (kQWidth - 1)) - 1;
  for (int bitIndex = kBitCount - 1; bitIndex >= 0; --bitIndex) {
    sum = sum.Double();
    const int digitP = nafP[bitIndex];
    const int digitQ = nafQ[bitIndex];
    if (digitP != 0) sum += scaled(P_table[(digitP + kPOffset) >> 1]);
    if (digitQ != 0) sum += Q_table[(digitQ + kQOffset) >> 1];
  }

  // All of the above point arithmetic took place using points on the scaled curve E_z.
  // To return the resulting point relative to the original curve E, we need to apply the map
  // (X, Y, Z) -> (X, Y, zZ)
  return { sum.X, sum.Y, z * sum.Z};
}

// Computes u1*G + u2*Q via GLV: a 4-term Strauss over two GLV terms (G-side, Q-side). Each term
// (glv.h) carries a lambda split plus the odd-multiple tables for its base and phi(base), and yields
// its wNAF digits (NonAdjacentFormDigits) and summands (Base/Phi). The accumulator is always
// Jacobian; both tables are affine so all the point additions are mixed types.
template <typename GTable, typename QTable, typename Element>
JacobianPoint<Element> LinearCombination_GLV(const GlvTerm<GTable, Element>& g, const GlvTerm<QTable, Element>& q) {
  using Point = JacobianPoint<Element>;

  Assert(g.global_z == 1);

  const auto [naf_g_a, naf_g_b] = g.NonAdjacentFormDigits();
  const auto [naf_q_a, naf_q_b] = q.NonAdjacentFormDigits();

  // The points in g's table are on the original curve E, whereas the points in q's table are on the
  // scaled curve, E_z, with z = q.global_z. To keep arithmetic consistent, we scale g's tabled points
  // on demand on to curve E_z.
  const auto& z = q.global_z;
  const auto z2 = z.Squared();
  const auto z3 = z2 * z;
  const auto scaled = [&](const AffinePoint<Element>& pt) -> AffinePoint<Element> { return { pt.x * z2, pt.y * z3 }; };

  // SplitLambda guarantees |k_i| < 2^(kBits/2), so the top wNAF digit index is <= kBits/2.
  Point sum;
  for (int bit = secp256k1::kBits >> 1; bit >= 0; --bit) {
    sum = sum.Double();
    if (naf_g_a[bit] != 0) sum += scaled(g.Base(naf_g_a[bit]));
    if (naf_g_b[bit] != 0) sum += scaled(g.Phi(naf_g_b[bit]));
    if (naf_q_a[bit] != 0) sum += q.Base(naf_q_a[bit]);
    if (naf_q_b[bit] != 0) sum += q.Phi(naf_q_b[bit]);
  }

  // All of the above point arithmetic took place using points on the scaled curve E_z.
  // To return the resulting point relative to the original curve E, we need to apply the map
  // (X, Y, Z) -> (X, Y, zZ)
  return { sum.X, sum.Y, z * sum.Z};
}

}  // namespace hornet::crypto::ecdsa
