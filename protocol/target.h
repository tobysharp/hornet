// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

#include "protocol/constants.h"
#include "protocol/hash.h"
#include "util/assert.h"
#include "util/big_uint.h"
#include "util/throw.h"

namespace hornet::protocol {

// Represents the 256-bit target value for a proof-of-work hash to achieve: hash <= target.
class Target {
 public:
  constexpr Target() = default;
  constexpr Target(const Target&) = default;
  constexpr Target(Target&&) = default;
  constexpr Target(const Uint256& value) : value_(value) {}

  // Expands a compact representation of target to a little-endian byte array.
  inline static constexpr Target FromCompact(uint32_t bits) {
    // This compact "bits" is a float-like representation of a 256-bit target.
    // The high 8 bits store the exponent, while the low 23 bits store the mantissa.
    // The intended value is given by:
    //   Target = 256^(Exponent - 3) * Mantissa, or equivalently:
    //   Target = 2^(8 * (Exponent - 3)) * Mantissa.
    // Bit 23 is interpreted as a sign bit, although negative values are inadmissible.
    // Note that overflow occurs when Mantissa > 0 and M + 8 * (Exponent - 3) > 256,
    // where M is the number of significant bits in the mantissa:
    //    M = floor(log2(Mantissa)) + 1.
    // i.e. Overflow = (Mantissa != 0) && (M > 280 - 8 * Exponent).
    // For Exponent >= 35, Overflow = (M > 0)  = (Mantissa > 0).
    // For Exponent == 34, Overflow = (M > 8)  = (Mantissa > 0xFF).
    // For Exponent == 33, Overflow = (M > 16) = (Mantissa > 0xFFFF).
    const int exponent = bits >> 24;
    const uint32_t mantissa = bits & 0x007fffff;
    const bool sign_bit = (bits & 0x00800000) != 0;
    const bool negative = mantissa > 0 && sign_bit;
    const bool overflow = mantissa > 0 && ((exponent > 34) || (exponent == 34 && mantissa > 0xFF) ||
                                           (exponent == 33 && mantissa > 0xFFFF));
    if (negative || mantissa == 0 || overflow) {
      // It's unforunate that for error conditions there isn't a value that we can return that
      // will fail the PoW test (hash <= target) for all hashes, since target is always unsigned.
      // Therefore we return an empty std::optional in this case.
      return {};
    }
    const int lshift_bits = 8 * (exponent - 3);
    const Uint256 target{mantissa};
    if (lshift_bits < 0)
      return target >> (-lshift_bits);
    else
      return target << lshift_bits;
  }

  // Compresses a target to a compact 32-bit representation.
  inline constexpr uint32_t GetCompact() const {
    // We define the exponent to be the number of significant bytes in Target.
    // Then we define the mantissa to be the most significant 3 bytes.
    // This sacrifices up to 7 bits of precision, but this is the Bitcoin Core method.
    const int significant_bytes = (value_->SignificantBits() + 7) >> 3;
    const int rshift_bytes = significant_bytes - 3;
    const int rshift_bits = rshift_bytes << 3;
    const Uint256 shifted_target =
        rshift_bits >= 0 ? (*value_ >> rshift_bits) : (*value_ << (-rshift_bits));
    int mantissa = shifted_target.Words()[0];
    int exponent = significant_bytes;
    // Now we have achieved Target = Mantissa * 2^(8 * (Exponent - 3)) + Error.
    // However, we are only allowed 23 bits for Mantissa, and we may have used 24.
    if (mantissa & 0x00800000) {
      // Yes, we used 24 bits, so we adjust down to 16 bits.
      mantissa >>= 8;
      ++exponent;
    }
    Assert((mantissa & ~0x007FFFFF) == 0);
    Assert(exponent < 256);
    return (exponent << 24) | mantissa;
  }

  // Convert from a little-endian hash representation
  inline static constexpr Target FromHash(const Hash& hash) {
    return Uint256{hash};
  }

  // Returns the maximum protocol-valid target value.
  static constexpr Target Maximum();

  // Determines whether the target value is protocol-valid.
  inline constexpr bool IsValid() const {
    return value_ && *this <= Maximum();
  }

  inline constexpr const Uint256& Value() const {
    if (!value_) util::ThrowRuntimeError("Target value empty.");
    return *value_;
  }

  // Returns the amount of work that must be done to achieve this target.
  constexpr inline Uint256 GetWork() const {
    if (!value_) return Uint256::Zero();
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
    return (~*value_ / (*value_ + 1u)) + 1u;
  }

  inline friend constexpr bool operator<=(const Hash& hash, const Target& rhs) {
    return rhs.value_ ? Uint256{hash} <= rhs.value_ : false;
  }

  inline friend constexpr bool operator>(const Hash& hash, const Target& rhs) {
    return rhs.value_ ? Uint256{hash} > rhs.value_ : false;
  }

  inline friend constexpr bool operator<=(const Target& lhs, const Target& rhs) {
    return lhs.value_ && rhs.value_ && lhs.value_ <= rhs.value_;
  }

 private:
  std::optional<Uint256> value_;
};

static constexpr Target kMaxProtocolTarget = Target::FromCompact(kMaxCompactTarget);

inline /* static */ constexpr Target Target::Maximum() {
  return kMaxProtocolTarget;
}

}  // namespace hornet::protocol
