// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/work.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/throw.h"

// Proof-of-work types and relationships:
//
// Hash -> compare leq -> Target <- .Expand() <- CompactTarget
//                          |
//                      .GetWork()
//                          |
//                         Work
//

namespace hornet::protocol {

// Represents the 256-bit target value for a proof-of-work hash to achieve: hash <= target.
class Target {
 public:
  constexpr Target() = default;
  constexpr Target(const Target&) = default;
  constexpr Target(Target&&) = default;
  constexpr Target(const Uint256& value) : value_(value) {}

  // Returns the maximum protocol-valid target value.
  static constexpr Target Maximum() {
    return kMaximumTarget;
  }

  // Determines whether the target value is protocol-valid.
  inline constexpr bool IsValid() const {
    return value_ && *this <= Maximum();
  }

  inline constexpr const Uint256& Value() const {
    if (!value_) util::ThrowRuntimeError("Target value empty.");
    return *value_;
  }

  // Returns the amount of work that must be done to achieve this target.
  constexpr inline Work GetWork() const {
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
    return !operator<=(hash, rhs);
  }

  inline friend constexpr bool operator<(const Target& lhs, const Target& rhs) {
    return lhs.value_ && rhs.value_ && lhs.value_ < rhs.value_;
  }

  inline friend constexpr bool operator<=(const Target& lhs, const Target& rhs) {
    return lhs.value_ && rhs.value_ && lhs.value_ <= rhs.value_;
  }

 private:
  std::optional<Uint256> value_;
};

}  // namespace hornet::protocol
