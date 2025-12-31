#pragma once

#include <array>

#include "hornetlib/util/timer.h"

namespace hornet::util {

template <int kCount>
class Metrics {
 public:
  Metrics& operator +=(const Metrics& rhs) {
    for (int op = 0; op < kCount; ++op) timers_[op] += rhs.timers_[op];
    return *this;
  }
  Metrics operator +(const Metrics& rhs) const {
    return Metrics{*this} += rhs;
  }
  auto AddScoped(int op) { return timers_[op].AddScoped(); }
  void Add(int index, auto&& func) {
    timers_[index].Add(std::move(func));
  }
  Timer& operator [](int index) { return timers_[index]; }
  const Timer& operator [](int index) const { return timers_[index]; }

 private:
  std::array<Timer, kCount> timers_;
};

}  // namespace hornet::util
