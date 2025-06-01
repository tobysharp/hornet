#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "protocol/constants.h"
#include "protocol/hash.h"
#include "util/big_uint.h"

namespace hornet::protocol {

// Represents the 256-bit target value for a proof-of-work hash to achieve: hash <= target.
class Target {
 public:
  constexpr Target() = default;
  constexpr Target(const Target&) = default;
  constexpr Target(Target&&) = default;
  constexpr Target(Uint256 value) : value_(std::move(value)) {}

  // Expands a compact representation of target to a little-endian byte array.
  inline static constexpr Target FromBits(uint32_t bits) {
    const uint32_t exponent = bits >> 24;
    const uint32_t mantissa = bits & 0x007fffff;
    if (mantissa == 0)
      return Uint256::Zero();     // Explicit edge case for compatibility.
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

  // Returns the maximum protocol-valid target value.
  static constexpr Target Maximum();

  // Determines whether the target value is protocol-valid.
  inline constexpr bool IsValid() const {
    return *this <= Maximum();
  }

  // Returns the amount of work that must be done to achieve this target.
  inline Uint256 GetWork() const {
    // Since SHA256 hashes are uniformly distributed, the expected number of 
    // hashes required to achieve hash <= target is given by the number of
    // independent Bernoulli trials required to get the first successful value.
    // This is modeled by a geometric distribution, which has E[X] = 1/p,
    // where p is the probability of success on any given trial.
    // Here, p = (target + 1) / 2^256, so Work = E[X] = 2^256 / (target + 1).
    // Now 2^256 / (target + 1) = (2^256 - target - 1) / (target + 1) + 1
    // = (2^256 - 1 - target) / (target + 1) + 1 = ~target / (target + 1) + 1.
    // This little rearrangement removes the need to represent 2^256, allowing us
    // to perform all arithmetic within our convenient 256-bit unsigned type.
    // And we use truncating integer division as usual.
    return (~value_ / (value_ + 1u)) + 1u;
  }

  inline constexpr bool operator<=(const Target& rhs) const {
    return value_ <= rhs.value_;
  }

 private:
  Uint256 value_ = Uint256::Zero();
};

static constexpr Target kMaxProtocolTarget = Target::FromBits(kMaxTargetBits);

inline /* static */ constexpr Target Target::Maximum() {
  return kMaxProtocolTarget;
}

}  // namespace hornet::protocol
