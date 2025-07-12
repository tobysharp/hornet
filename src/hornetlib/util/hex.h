// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
// Compile-time parsing of hexadecimal string literals.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/unroll.h"

namespace hornet {
namespace util {

// ---- Hex digit decoder ----
template <char C>
inline consteval uint8_t HexValue() {
  constexpr bool valid = (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F');
  static_assert(valid, "Invalid hex digit.");

  if constexpr (C >= '0' && C <= '9')
    return C - '0';
  else if constexpr (C >= 'a' && C <= 'f')
    return C - 'a' + 10;
  else
    return C - 'A' + 10;
}

// ---- Index sequence helper ----
template <size_t N, typename F>
inline consteval auto ApplyToSequence(F&& f) {
  return [&]<size_t... I>(std::index_sequence<I...>) {
    // Forward as template parameters
    return f.template operator()<I...>();
  }(std::make_index_sequence<N>{});
}

// ---- Hex decoder engine ----
template <char... Cs>
inline consteval auto DecodeHexString(bool reverse = true) {
  static_assert(sizeof...(Cs) % 2 == 0, "Hex string must have even length.");
  constexpr char str[] = {Cs...};
  constexpr int length = sizeof...(Cs) / 2;
  std::array<uint8_t, length> result;
  Unroll<length>([&](auto kIndex) {
    const auto dst_index = reverse ? length - 1 - kIndex : kIndex;
    result[dst_index] = HexValue<str[2 * kIndex]>() << 4 | HexValue<str[2 * kIndex + 1]>();
  });
  return result;
}

// ---- Hex digit filtering ----

template <char c>
inline consteval bool IsWhitespace() {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

// Forward declaration of recursive metafunction.
template <typename Accum, char... Cs>
struct FilterHexImpl;

// Base case: no input chars left, output array is the accumulated chars.
template <char... Accum>
struct FilterHexImpl<std::integer_sequence<char, Accum...>> {
  inline static constexpr std::array<char, sizeof...(Accum)> value{Accum...};
};

// Recursive case: skip whitespace, keep other characters, recurse on Tail.
template <char... Accum, char Head, char... Tail>
struct FilterHexImpl<std::integer_sequence<char, Accum...>, Head, Tail...> {
  using Next = std::conditional_t<IsWhitespace<Head>(), std::integer_sequence<char, Accum...>,
                                  std::integer_sequence<char, Accum..., Head>>;
  inline static constexpr auto value = FilterHexImpl<Next, Tail...>::value;
};

template <char... Cs>
inline consteval auto StripWhitespace() {
  return FilterHexImpl<std::integer_sequence<char>, Cs...>::value;
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

// _hash: 256-bit, big endian, returns std::array<uint8_t, 32>.
template <util::HexLiteral<65> H>
consteval std::array<uint8_t, 32> operator""_hash() {
  constexpr auto& chars = H.chars;
  return util::ApplyToSequence<64>(
      [&]<size_t... I>() { return util::DecodeHexString<chars[I]...>(true); });
}

//_h256: 256-bit, big endian, returns Uint256.
template <util::HexLiteral<65> H>
inline consteval Uint256 operator""_h256() {
  constexpr auto& chars = H.chars;
  return Uint256{util::ApplyToSequence<64>(
      [&]<size_t... I>() { return util::DecodeHexString<chars[I]...>(true); })};
}

// _bytes: variable-length, memory order, strips whitespace, returns std::array<uint8_t, N>.
template <util::HexLiteral H>
inline consteval auto operator""_bytes() {
  constexpr auto& chars = H.chars;
  return util::ApplyToSequence<chars.size() - 1>([&]<size_t... I>() {
    constexpr auto filtered = util::StripWhitespace<chars[I]...>();
    return util::ApplyToSequence<filtered.size()>(
        [&]<size_t... J>() { return util::DecodeHexString<filtered[J]...>(/*reverse=*/false); });
  });
}

}  // namespace hornet
