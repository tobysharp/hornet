#pragma once

#include <bit>
#include <iterator>
#include <span>

#include "hornetlib/crypto/naf.h"
#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

template <class Point>
constexpr Point Scale(const typename Point::Wide& scalar, const Point& pt) {
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

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a>
constexpr JacobianPoint<kBits, p, a> LinearCombination(const UIntW<kBits>& u1, const AffinePoint<kBits, p, a>& P,
                                                          const UIntW<kBits>& u2,
                                                          const AffinePoint<kBits, p, a>& Q) {
  using Affine = AffinePoint<kBits, p, a>;
  using Point = JacobianPoint<kBits, p, a>;
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

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a>
constexpr JacobianPoint<kBits, p, a> LinearCombination_NAF_Disjoint(const UIntW<kBits>& u1, const AffinePoint<kBits, p, a>& P,
                                                          const UIntW<kBits>& u2,
                                                          const AffinePoint<kBits, p, a>& Q) {
  using Affine = AffinePoint<kBits, p, a>;
  using Point = JacobianPoint<kBits, p, a>;
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
template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a>
JacobianPoint<kBits, p, a> LinearCombination_wNAF(const UIntW<kBits>& u1,
                                                  std::span<const AffinePoint<kBits, p, a>> P_table,
                                                  const UIntW<kBits>& u2, const AffinePoint<kBits, p, a>& Q) {
  using Point = JacobianPoint<kBits, p, a>;
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

}  // namespace hornet::crypto::ecdsa
