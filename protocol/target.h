#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "protocol/constants.h"
#include "util/big_uint.h"

namespace hornet::protocol {

// A representation of a 256-bit target value.
class Target {
 public:
  constexpr Target() : value_(Uint256::Zero()) {}
  constexpr Target(const Target&) = default;
  constexpr Target(Target&&) = default;
  constexpr Target(Uint256 value) : value_(std::move(value)) {}

  operator Uint256() const {
    return value_;
  }

  // Expands a compact representation of target to a little-endian byte array.
  inline static constexpr Target FromBits(uint32_t bits) {
    const uint32_t exponent = bits >> 24;
    const uint32_t mantissa = bits & 0x007fffff;
    if (mantissa == 0)
      return Uint256::Zero();  // Explicit edge case for compatibility.
    else if (exponent > 32)
      return Uint256::Maximum();  // Invalid target.
    else if (exponent > 0x1D || (exponent == 0x1D && mantissa > 0xFFFF))
      return Target::Maximum();  // Maximum protocol-valid target value.
    const uint32_t explicit_mantissa = mantissa | (1u << 23);
    const int lshift_bits = 8 * (exponent - 3);
    const Uint256 target_mantissa{explicit_mantissa};
    if (lshift_bits < 0)
      return target_mantissa >> (-lshift_bits);
    else
      return target_mantissa << lshift_bits;
  }

  // Convert from a little-endian hash representation
  inline static constexpr Target FromHash(Hash hash) {
    return Uint256{std::move(hash)};
  }

  static constexpr Target Maximum();

  inline constexpr bool IsValid() const {
    return *this <= Maximum();
  }

  inline Uint256 GetWork() const {
    return (~value_ / (value_ + 1)) + 1;
  }

  inline constexpr bool operator<=(const Target& rhs) const {
    return value_ <= rhs.value_;
  }

 private:
  Uint256 value_;
};

static constexpr Target kMaxProtocolTarget = Target::FromBits(kMaxTargetBits);

inline /* static */ constexpr Target Target::Maximum() {
  return kMaxProtocolTarget;
}

inline bool IsProofOfWork(const Hash& hash, uint32_t bits) {
  return Target::FromHash(hash) <= Target::FromBits(bits);
}

}  // namespace hornet::protocol
