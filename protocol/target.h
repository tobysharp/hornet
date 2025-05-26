#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "protocol/constants.h"

namespace hornet::protocol {

// A big-endian byte representation of a 256-bit target value.
struct Target {
  std::array<uint8_t, 32> bytes;

  // Expands a compact representation of target to a big-endian byte array.
  inline static constexpr Target FromBits(uint32_t bits) {
    uint32_t exponent = bits >> 24;
    uint32_t mantissa = bits & 0x007fffff;

    Target target = {};
    if (mantissa == 0 || exponent < 3 || exponent > 34)
      return target;  // Invalid target → all zeros

    if (exponent <= 32) {
      target.bytes[exponent - 3] = static_cast<uint8_t>(mantissa >> 16);
      target.bytes[exponent - 2] = static_cast<uint8_t>(mantissa >> 8);
      target.bytes[exponent - 1] = static_cast<uint8_t>(mantissa);
    }

    return target;
  }

  // Convert from a little-endian hash representation
  inline static constexpr Target FromHash(Hash hash) {
    std::reverse(hash.begin(), hash.end());
    return Target {.bytes = std::move(hash)};
  }

  inline static constexpr Target Maximum() {
    return Target::FromBits(0x1d00ffff);
  }

  bool IsValid() const {
    return *this <= Maximum();
  }

  inline friend bool operator <=(const Target& lhs, const Target& rhs) {
    return std::memcmp(&lhs.bytes[0], &rhs.bytes[0], sizeof(lhs.bytes)) <= 0;
  }
};

inline bool IsProofOfWork(const Hash& hash, uint32_t bits) {
  return Target::FromHash(hash) <= Target::FromBits(bits);
}


}  // namespace hornet::protocol