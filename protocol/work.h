#pragma once

#include "protocol/target.h"
#include "util/big_uint.h"

namespace hornet::protocol {

// Represents the amount of work done to achieve a given target.
class Work {
 public:
  constexpr Work() = default;
  constexpr Work(const Work&) = default;
  constexpr Work(Work&&) = default;
  constexpr Work(const Uint256& value) : value_(value) {}

  // Return a Work object from a compact "bits" representation of a target.
  static constexpr Work FromBits(uint32_t bits) {
    return Target::FromCompact(bits).GetWork();
  }

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