#pragma once

#include <functional>
#include <type_traits>

namespace hornet::util {

template <typename T, typename F>
constexpr auto Sum(T begin, T end, F&& func) {
  using S = std::remove_cvref_t<std::invoke_result_t<F&, T>>;

  S sum{};
  for (T i = begin; i != end; ++i)
    sum += std::invoke(func, i);
  return sum;
}

template <typename T, typename F>
constexpr auto Sum(T range, F&& func) {
  using X = decltype(*std::begin(range));
  using S = std::remove_cvref_t<std::invoke_result_t<F&, X>>;

  S sum{};
  for (const auto& x : range)
    sum += std::invoke(func, x);
  return sum;
}

}  // namespace hornet::util
