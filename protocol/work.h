#pragma once

#include "protocol/target.h"
#include "util/big_uint.h"

namespace hornet::protocol {

class Work {
 public:
  constexpr Work() = default;
  constexpr Work(const Work&) = default;
  constexpr Work(Work&&) = default;
  constexpr Work(Uint256 value) : value_(std::move(value)) {}

  constexpr Work& operator=(const Work&) = default;
  constexpr Work& operator=(Work&&) = default;

  static constexpr Work FromBits(uint32_t bits) {
    return Target::FromBits(bits).GetWork();
  }

  constexpr Work operator+(const Work& rhs) const {
    return value_ + rhs.value_;
  }
  constexpr Work& operator+=(const Work& rhs) {
    value_ += rhs.value_;
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