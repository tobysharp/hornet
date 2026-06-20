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
template <int kBits>
constexpr auto NonAdjacentForm(const UIntW<kBits>& x) noexcept {

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

// Returns the width-w NAF (wNAF) of x in little-endian digit order.
// Every nonzero digit is an odd integer in {+-1, +-3, ..., +-(2^{w-1}-1)}, which reaches
// +-511 at w=10 -- too wide for int8_t, so digits are stored as int16_t.
template <int kBits>
constexpr auto WindowedNonAdjacentForm(const UIntW<kBits>& x, int w) noexcept {
  std::array<int16_t, kBits + 1> naf = {};

  const int high = 1 << w;        // 2^w
  const int half = 1 << (w - 1);  // 2^{w-1}

  bool carry = false;
  for (int i = 0; i < kBits; ++i) {
    if (x.GetBit(i) != carry) {
      int window = 1;
      for (int j = 1; j < std::min(w, kBits - i); ++j) window += x.GetBit(i + j) << j;
      const int digit = ((window + half) & (high - 1)) - half;
      naf[i] = digit;
      carry = (window - digit) >> w;
      i += w - 1;
    }
  }
  naf[kBits] = carry;
  return naf;
}

template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a>
void PrecomputeTableJacobian(const AffinePoint<kBits, p, a>& P, std::span<JacobianPoint<kBits, p, a>> table) {
  if (table.size() < 2u) return;

  // The table size is 2^(w-1) for window size w, to include both negative and positive odd multiples.
  Assert(std::has_single_bit(table.size()));
  const int size = std::ssize(table);
  const int count = size >> 1;

  // We compute the odd-multiple points, {..., -5P, -3P, -P, P, 3P, 5P, ... } to fill the table.
  const auto P2 = JacobianPoint<kBits, p, a>{P}.Double();
  table[count] = P;
  table[count - 1] = -P;
  for (int i = 1; i < count; ++i) {
    table[count + i] = table[count + i - 1] + P2;
    table[count - 1 - i] = -table[count + i];
  }
}

// Builds the wide fixed-base odd-multiple table in affine form: { ..., -3P, -P, P, 3P, ... }.
// Intended to be precomputed once for a fixed base (e.g. the generator) and passed into the
// wNAF linear combination, so the per-table normalization cost is amortized across many calls.
template <int kBits, const UIntW<kBits>& p, const UIntW<kBits>& a>
void PrecomputeTableAffine(const AffinePoint<kBits, p, a>& P, std::span<AffinePoint<kBits, p, a>> table) {
  std::vector<JacobianPoint<kBits, p, a>> jacobian(table.size());
  PrecomputeTableJacobian(P, std::span{jacobian});
  for (int i = 0; i < std::ssize(table); ++i) table[i] = jacobian[i];  // Jacobian -> affine
}

}  // namespace hornet::crypto::ecdsa
