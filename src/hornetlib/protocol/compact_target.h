// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/target.h"
#include "hornetlib/util/big_uint.h"

// Proof-of-work types and relationships:
//
// Hash -> compare leq -> Target <- .Expand() <- CompactTarget
//                          |
//                      .GetWork()
//                          |
//                         Work
//

namespace hornet::protocol {

class CompactTarget {
 public:
  constexpr CompactTarget() {}
  constexpr CompactTarget(uint32_t bits) : bits_(bits) {}
  constexpr CompactTarget(const Target& target) : CompactTarget(Compress(target)) {}
  constexpr CompactTarget(const CompactTarget&) = default;

  constexpr CompactTarget& operator =(const CompactTarget&) = default;

  // Compresses a target to a compact 32-bit representation.
  static constexpr CompactTarget Compress(const Target& target) {
    Assert(target.IsValid());
    // We define the exponent to be the number of significant bytes in Target.
    // Then we define the mantissa to be the most significant 3 bytes.
    // This sacrifices up to 7 bits of precision, but this is the Bitcoin Core method.
    const Uint256 value = target.Value();
    const int significant_bytes = (value.SignificantBits() + 7) >> 3;
    const int rshift_bytes = significant_bytes - 3;
    const int rshift_bits = rshift_bytes << 3;
    const Uint256 shifted_target =
        rshift_bits >= 0 ? (value >> rshift_bits) : (value << (-rshift_bits));
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
    uint32_t bits = static_cast<uint32_t>((exponent << 24) | mantissa);
    return CompactTarget{bits};
  }

  // Expands a compact 32-bit target to a full 256-bit target.
  constexpr Target Expand() const {
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
    const int exponent = bits_ >> 24;
    const uint32_t mantissa = bits_ & 0x007fffff;
    const bool sign_bit = (bits_ & 0x00800000) != 0;
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
    const Uint256 rv = (lshift_bits < 0) ? (target >> (-lshift_bits)) : (target << lshift_bits);
    return Target{rv};
  }

  constexpr uint32_t Value() const { return bits_; }
  
  void Serialize(encoding::Writer& writer) const {
    writer.WriteLE4(bits_);
  }

  void Deserialize(encoding::Reader& reader) {
    reader.ReadLE4(bits_);
  }

  constexpr bool operator ==(const CompactTarget& rhs) const {
    return bits_ == rhs.bits_;
  }

  constexpr bool operator !=(const CompactTarget& rhs) const {
    return bits_ != rhs.bits_;
  }

  static CompactTarget Maximum() {
    return kMaxCompactTarget;
  }

 private:
  uint32_t bits_ = 0;
};

inline constexpr CompactTarget CompressTarget(const Target& target) {
  return CompactTarget::Compress(target);
}

}  // namespace hornet::protocol
