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

constexpr JacobianPoint LinearCombination(const UInt256& u1, const AffinePoint& P,
                                                          const UInt256& u2,
                                                          const AffinePoint& Q) {
  using Affine = AffinePoint;
  using Point = JacobianPoint;
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

constexpr JacobianPoint LinearCombination_NAF_Disjoint(const UInt256& u1, const AffinePoint& P,
                                                          const UInt256& u2,
                                                          const AffinePoint& Q) {
  using Affine = AffinePoint;
  using Point = JacobianPoint;
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
inline JacobianPoint LinearCombination_wNAF(const UInt256& u1,
                                                  std::span<const AffinePoint> P_table,
                                                  const UInt256& u2, const AffinePoint& Q) {
  using Point = JacobianPoint;
  constexpr int kQWidth = 5;
  const int kPWidth = std::bit_width(P_table.size());  // 2^{w-1} entries -> window width w
  const auto nafP = WindowedNonAdjacentForm(u1, kPWidth);
  const auto nafQ = WindowedNonAdjacentForm(u2, kQWidth);
  constexpr int kBitCount = std::ssize(nafP);

  std::array<Point, 1 << (kQWidth - 1)> Q_table;
  PrecomputeTableJacobian(Q, {Q_table.data(), Q_table.size()});

  Point sum;
  const int kPOffset = std::ssize(P_table) - 1;
  constexpr int kQOffset = (1 << (kQWidth - 1)) - 1;
  for (int bitIndex = kBitCount - 1; bitIndex >= 0; --bitIndex) {
    sum = sum.Double();
    const int digitP = nafP[bitIndex];
    const int digitQ = nafQ[bitIndex];
    if (digitP != 0) sum += P_table[(digitP + kPOffset) >> 1];
    if (digitQ != 0) sum += Q_table[(digitQ + kQOffset) >> 1];
  }
  return sum;
}

// Computes u1*G + u2*Q via GLV: a 4-term Strauss over two GLV terms (G-side, Q-side). Each term
// (glv.h) carries a lambda split plus the odd-multiple tables for its base and phi(base), and yields
// its wNAF digits (NonAdjacentFormDigits) and summands (Base/Phi). The accumulator is always
// Jacobian; G's affine table yields mixed adds.
template <class GTable, class QTable>
JacobianPoint LinearCombination_GLV(const GlvTerm<GTable>& g,
                                                 const GlvTerm<QTable>& q) {
  using Point = JacobianPoint;
  const auto [naf_g_a, naf_g_b] = g.NonAdjacentFormDigits();
  const auto [naf_q_a, naf_q_b] = q.NonAdjacentFormDigits();

  // SplitLambda guarantees |k_i| < 2^(kBits/2), so the top wNAF digit index is <= kBits/2.
  Point sum;
  for (int bit = secp256k1::kBits >> 1; bit >= 0; --bit) {
    sum = sum.Double();
    if (naf_g_a[bit] != 0) sum += g.Base(naf_g_a[bit]);
    if (naf_g_b[bit] != 0) sum += g.Phi(naf_g_b[bit]);
    if (naf_q_a[bit] != 0) sum += q.Base(naf_q_a[bit]);
    if (naf_q_b[bit] != 0) sum += q.Phi(naf_q_b[bit]);
  }
  return sum;
}

}  // namespace hornet::crypto::ecdsa
