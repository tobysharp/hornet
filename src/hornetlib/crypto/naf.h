#pragma once

#include <array>
#include <concepts>
#include <cstdint>

#include "hornetlib/util/big_uint.h"

namespace hornet::crypto::ecdsa {

// Returns the plain signed-digit NAF encoding of x in little-endian digit order.
// The extra high digit stores any final carry introduced by the recoding.
template <int kBits, std::unsigned_integral T>
constexpr auto NonAdjacentForm(const util::BigUint<kBits, T>& x) noexcept {

  std::array<int8_t, kBits + 1> naf = {};
  bool carry = false;
  bool bit0 = x.GetBit(0);
  for (int i = 0; i < kBits; ++i) {
    const bool bit1 = i + 1 < kBits && x.GetBit(i + 1);
    if (bit0 != carry) {
      const int8_t mod4 = bit0 + carry + (bit1 << 1);
      const int8_t digit = 2 - mod4;
      naf[i] = digit;
      carry = digit < 0;
    }
    bit0 = bit1;
  }
  naf[kBits] = carry;
  return naf;
}

}  // namespace hornet::crypto::ecdsa
