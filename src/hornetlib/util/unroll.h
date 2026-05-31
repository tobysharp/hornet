// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <type_traits>
#include <utility>

namespace hornet::util {

template <int Offset, int... Is, typename F, typename... Args>
[[clang::always_inline]] constexpr void UnrollImpl(std::integer_sequence<int, Is...>, F&& f, Args&&... args)
    noexcept((noexcept(f(std::integral_constant<int, Offset + Is>{}, args...)) && ...)) {
  (f(std::integral_constant<int, Offset + Is>{}, args...), ...);
}

template <int Start, int End, typename F, typename... Args>
[[clang::always_inline]] constexpr void UnrollRange(F&& f, Args&&... args)
    noexcept(noexcept(UnrollImpl<Start>(
        std::make_integer_sequence<int, End - Start>{},
        std::forward<F>(f),
        std::forward<Args>(args)...))) {
  static_assert(Start <= End);
  UnrollImpl<Start>(
      std::make_integer_sequence<int, End - Start>{},
      std::forward<F>(f),
      std::forward<Args>(args)...);
}

template <int N, typename F, typename... Args>
[[clang::always_inline]] constexpr void Unroll(F&& f, Args&&... args)
    noexcept(noexcept(UnrollImpl<0>(
        std::make_integer_sequence<int, N>{},
        std::forward<F>(f),
        std::forward<Args>(args)...))) {
  UnrollImpl<0>(
      std::make_integer_sequence<int, N>{},
      std::forward<F>(f),
      std::forward<Args>(args)...);
}

}  // namespace hornet::util
