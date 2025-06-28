// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

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

// Represents the amount of work done to achieve a given target.
class Work {
 public:
  constexpr Work() = default;
  constexpr Work(const Work&) = default;
  constexpr Work(Work&&) = default;
  constexpr Work(const Uint256& value) : value_(value) {}

  // Exposes only the operators that are appropriate for work done.

  constexpr Work& operator=(const Work&) = default;
  constexpr Work& operator=(Work&&) = default;
  constexpr Work operator+(const Work& rhs) const {
    return value_ + rhs.value_;
  }
  constexpr Work operator-(const Work& rhs) const {
    return value_ - rhs.value_;
  }
  constexpr Work& operator+=(const Work& rhs) {
    value_ += rhs.value_;
    return *this;
  }
  constexpr Work& operator -=(const Work& rhs) {
    value_ -= rhs.value_;
    return *this;
  }
  constexpr bool operator<(const Work& rhs) const {
    return value_ < rhs.value_;
  }
  constexpr bool operator<=(const Work& rhs) const {
    return value_ <= rhs.value_;
  }
  constexpr bool operator>(const Work& rhs) const {
    return value_ > rhs.value_;
  }
  constexpr bool operator>=(const Work& rhs) const {
    return value_ >= rhs.value_;
  }
  constexpr bool operator==(const Work& rhs) const {
    return value_ == rhs.value_;
  }
  constexpr bool operator!=(const Work& rhs) const {
    return value_ != rhs.value_;
  }

 private:
  Uint256 value_ = Uint256::Zero();
};

}  // namespace hornet::protocol
