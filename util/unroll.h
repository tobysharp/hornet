#pragma once

#include <type_traits>
#include <utility>

namespace hornet::util {

template <int... kIndices, typename Function>
constexpr void UnrollImpl(std::integer_sequence<int, kIndices...>, Function&& function) {
    (function(std::integral_constant<int, kIndices>{}), ...);
}

template <int kCount, typename Function>
constexpr void Unroll(Function&& function) {
    UnrollImpl(std::make_integer_sequence<int, kCount>{}, std::forward<Function>(function));
}

}  // namespace hornet::util
