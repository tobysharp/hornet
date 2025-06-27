// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
// Compile-time parsing of hexadecimal string literals.

#pragma once

#include <cstddef>
#include <cstdint>

#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/unroll.h"

namespace hornet {
namespace util {

// ---- Hex digit decoder ----
template <char C>
consteval uint8_t HexValue() {
  constexpr bool valid = (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F');
  static_assert(valid, "Invalid hex digit.");

  if constexpr (C >= '0' && C <= '9')
    return C - '0';
  else if constexpr (C >= 'a' && C <= 'f')
    return C - 'a' + 10;
  else
    return C - 'A' + 10;
}

// ---- Hex decoder engine ----
template <char... Cs>
consteval auto DecodeHexString() {
  static_assert(sizeof...(Cs) % 2 == 0, "Hex string must have even length.");
  constexpr char str[] = {Cs...};
  constexpr int length = sizeof...(Cs) / 2;
  std::array<uint8_t, length> result;
  Unroll<length>([&](auto kIndex) {
    result[length - 1 - kIndex] = HexValue<str[2 * kIndex]>() << 4 | HexValue<str[2 * kIndex + 1]>();
  });
  return result;
}

// ---- HexLiteral wrapper ----
template <int kChars>
struct HexLiteral {
  std::array<char, kChars> chars;

  consteval HexLiteral(const char (&input)[kChars]) : chars{} {
    std::copy(input, input + kChars, chars.begin());
  }
};

}  // namespace util

// _h: variable-length, returns std::array<uint8_t, N>
template <util::HexLiteral H>
consteval auto operator""_h() {
  constexpr auto& chars = H.chars;
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return util::DecodeHexString<chars[I]...>();
  }(std::make_index_sequence<chars.size() - 1>{});
}

// _h256: exactly 64 hex digits, returns Uint256
template <util::HexLiteral<65> H>
consteval Uint256 operator""_h256() {
  constexpr auto& chars = H.chars;
  return Uint256{[]<std::size_t... I>(std::index_sequence<I...>) {
    return util::DecodeHexString<chars[I]...>();
  }(std::make_index_sequence<64>{})};
}

}  // namespace hornet
