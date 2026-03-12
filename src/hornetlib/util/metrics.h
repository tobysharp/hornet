#pragma once

#include <array>
#include <numeric>

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
  auto Add(int index, auto&& func) {
    return timers_[index].Add(std::forward<decltype(func)>(func));
  }
  Timer& operator [](int index) { return timers_[index]; }
  const Timer& operator [](int index) const { return timers_[index]; }

  double TotalCpuSeconds() const {
    return std::accumulate(timers_.begin(), timers_.end(), 0.0,
                           [](double sum, const auto& t) { return sum + t.CpuSeconds().count(); });
  }
  
  double TotalSeconds() const {
    return std::accumulate(timers_.begin(), timers_.end(), 0.0,
                           [](double sum, const auto& t) { return sum + t.Seconds().count(); });
  }

 private:
  std::array<Timer, kCount> timers_;
};

}  // namespace hornet::util
