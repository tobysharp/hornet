// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <type_traits>
#include <utility>

namespace hornet::util {

template <int Start, int End, typename Function>
constexpr void UnrollRange(Function&& function) {
  if constexpr (Start < End) {
    function(std::integral_constant<int, Start>{});
    UnrollRange<Start + 1, End>(std::forward<Function>(function));
  }
}

template <int kCount, typename Function>
constexpr void Unroll(Function&& function) {
  UnrollRange<0, kCount>(std::forward<Function>(function));
}

}  // namespace hornet::util
