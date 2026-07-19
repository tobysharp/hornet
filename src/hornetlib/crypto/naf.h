#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/crypto/point.h"
#include "hornetlib/crypto/uintw.h"

namespace hornet::crypto::ecdsa {

// Returns the plain signed-digit NAF encoding of x in little-endian digit order.
// The extra high digit stores any final carry introduced by the recoding.
template <int kBits> constexpr auto NonAdjacentForm(const UIntW<kBits>& x) noexcept {
  std::array<int16_t, kBits + 1> naf = {};
  bool carry = false;
  bool bit0 = x.GetBit(0);
  for (int i = 0; i < kBits; ++i) {
    const bool bit1 = i + 1 < kBits && x.GetBit(i + 1);
    if (bit0 != carry) {
      const int8_t mod4 = 1 + (bit1 << 1);
      const int8_t digit = 2 - mod4;
      naf[i] = digit;
      carry = digit < 0;
    }
    bit0 = bit1;
  }
  naf[kBits] = carry;
  return naf;
}

// Returns the width-w NAF (wNAF) of x, negated if `negative`, in little-endian digit order.
// Every nonzero digit is an odd integer in {+-1, +-3, ..., +-(2^{w-1}-1)}, which reaches
// +-511 at w=10 -- too wide for int8_t, so digits are stored as int16_t.
template <int kBits>
constexpr auto WindowedNonAdjacentForm(const UIntW<kBits>& x, int w, bool negative = false) noexcept {
  std::array<int16_t, kBits + 1> naf = {};

  const int high = 1 << w;        // 2^w
  const int half = 1 << (w - 1);  // 2^{w-1}

  bool carry = false;
  for (int i = 0; i < kBits; ++i) {
    if (x.GetBit(i) != carry) {
      int window = 1;
      for (int j = 1; j < std::min(w, kBits - i); ++j) window += x.GetBit(i + j) << j;
      const int digit = ((window + half) & (high - 1)) - half;
      naf[i] = negative ? -digit : digit;
      carry = (window - digit) >> w;
      i += w - 1;
    }
  }
  naf[kBits] = negative ? -int{carry} : int{carry};
  return naf;
}

inline FieldElement PrecomputeTableGlobalZ(const AffinePoint& P, std::span<AffinePoint> table) {
  // The table size is 2^(w-1) for window size w, to include both negative and positive odd multiples.
  // We compute the odd-multiple points, {..., -5P, -3P, -P, P, 3P, 5P, ... } to fill the table.

  Assert(table.size() >= 2u);
  Assert(std::has_single_bit(table.size()));

  const int size = std::ssize(table);
  const int count = size >> 1;

  AffinePoint* positives = &table[count];  // positives[i]  =  (2i + 1)P, i \in [0, count).
  AffinePoint* negatives = positives - 1;  // negatives[-i] = -(2i + 1)P, i \in [0, count).
  std::vector<JacobianPoint> sums(count);
  std::vector<FieldElement> ratios(count);

  // To allow us to perform the point additions as affine points rather than Jacobian points,
  // we map all points to the scaled curve, E_C: y^2 = x^3 + bC^6, using the map,
  // λ_C : (X, Y, Z) |-> (X, Y, Z/C), or equivalently,
  // λ_C : (X, Y, Z) |-> (C^2X, C^3Y, Z).

  const JacobianPoint P2 = JacobianPoint{P}.Double();
  const auto C = P2.Z;
  const auto C2 = C.Squared();
  const auto C3 = C * C2;
  const AffinePoint a2P = {P2.X, P2.Y};  // λ_C(2P)

  // Perform affine additions on curve E_C, storing ratios[i] = Z_i / Z_{i-1}.
  sums[0] = {P.x * C2, P.y * C3, 1};                                                              // λ_C(P)
  for (int i = 1; i < count; ++i) std::tie(sums[i], ratios[i]) = sums[i - 1].AddWithZRatio(a2P);  // λ_C((2i + 1)P)

  // Scale all points to a shared z coordinate on E_C, g_C = Z_last, and then apply the additional map,
  // λ_{g_C} to represent the points as affine, i.e. with an implicit Z=1 parameter.
  const auto g_C = sums[count - 1].Z;
  FieldElement scale = 1;  // scale = g_C / sums[count - 1].Z
  positives[count - 1] = {sums[count - 1].X.NormalizeWeak(), sums[count - 1].Y.NormalizeWeak()};
  negatives[1 - count] = {positives[count - 1].x, -positives[count - 1].y};
  for (int i = count - 2; i >= 0; --i) {
    // To scale sums[i] to have a Z coordinate of g_C, we need to multiply its Z value by t = g_C / Z_i,
    // and since (X, Y, Z) ~ (t^2X, t^3Y, tZ), we therefore must scale its X value by t^2, and Y value by t^3.
    // But t = g_C / Z_i = g_C / Z_{i+1} * (Z_{i+1} / Z_i) = g_C / Z_{i+1} * ratios[i+1].
    scale *= ratios[i + 1];  // scale = g_C / Z_i
    const auto scale2 = scale.Squared();
    const auto px = sums[i].X * scale2;
    const auto py = sums[i].Y * scale2 * scale;
    positives[i] = {px, py};  // Applies λ_{g_C}.
    negatives[-i] = {px, -py};
  }

  // Now we have applied λ_C followed by λ_{g_C}, which is the composite λ_{C * g_C}.
  // This composite scaling factor g = C * g_C, determines the inverse map that we will need to apply
  // later to the result of any point arithmetic in order to map back to the original curve, E.
  // Specifically, apply the inverse map, λ^{-1}_g : (X, Y, Z) |-> (X, Y, gZ).
  return C * g_C;
}

inline void PrecomputeTableJacobian(const AffinePoint& P, std::span<JacobianPoint> table) {
  if (table.size() < 2u) return;

  // The table size is 2^(w-1) for window size w, to include both negative and positive odd multiples.
  Assert(std::has_single_bit(table.size()));
  const int size = std::ssize(table);
  const int count = size >> 1;

  // We compute the odd-multiple points, {..., -5P, -3P, -P, P, 3P, 5P, ... } to fill the table.
  const auto P2 = JacobianPoint{P}.Double();
  table[count] = P;
  table[count - 1] = {P.x, -P.y};
  for (int i = 1; i < count; ++i) {
    table[count + i] = table[count + i - 1] + P2;
    table[count - 1 - i] = -table[count + i];
  }
}

// Builds the wide fixed-base odd-multiple table in affine form: { ..., -3P, -P, P, 3P, ... }.
// Intended to be precomputed once for a fixed base (e.g. the generator) and passed into the
// wNAF linear combination, so the per-table normalization cost is amortized across many calls.
inline void PrecomputeTableAffine(const AffinePoint& P, std::span<AffinePoint> table) {
  std::vector<JacobianPoint> jacobian(table.size());
  PrecomputeTableJacobian(P, std::span{jacobian});
  for (int i = 0; i < std::ssize(table); ++i) table[i] = jacobian[i];  // Jacobian -> affine
}

}  // namespace hornet::crypto::ecdsa
