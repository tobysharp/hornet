#pragma once

#include <functional>
#include <type_traits>

namespace hornet::util {

template <typename T, typename F>
constexpr auto Sum(T begin, T end, F&& func) {
  using S = std::remove_cvref_t<std::invoke_result_t<F&, T>>;

  S sum{};
  for (T i = begin; i != end; ++i) {
    sum += std::invoke(func, i);
  }
  return sum;
}

}  // namespace hornet::util
