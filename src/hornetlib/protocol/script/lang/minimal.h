// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <span>

#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

namespace hornet::protocol::script::lang {

namespace detail {

// Returns the negative of an unsigned input.
// Safe for all input values up to 0x80...00.
template <std::unsigned_integral U>
inline constexpr auto Negate(U value) noexcept {
  using T = std::make_signed_t<U>;
  return T(~value + 1);
}

// Returns the unsigned absolute value of any integer.
// Unlike std::abs, safe for extreme values, i.e. Abs : (int8_t)-128 --> (uint8_t)128.
template <std::integral T>
inline constexpr auto Abs(T value) noexcept {
  using U = std::make_unsigned_t<T>;
  // Uses twos-complement conversion to avoid UB on INT_MIN.
  return value < T{0} ? ~U(value) + 1 : U(value);
}

}  // namespace detail

template <std::integral T>
struct MinimalIntEncoded {
  void Append(uint8_t value) {
    Assert(size < std::ssize(bytes));
    bytes[size++] = value;
  }
  operator std::span<const uint8_t>() const {
    return {&bytes[0], size_t(size)};
  }
  std::array<uint8_t, sizeof(T) + 1> bytes;
  int size = 0;
};

// Encodes a compile-time small integer in the minimal encoding format. 
template <int8_t N>
constexpr inline uint8_t EncodeMinimalConst() {
  static_assert(N == -1 || (N >= 1 && N <= 16));
  if constexpr (N == -1) return 0x81;
  return uint8_t(N);
}

// Encodes the integer in the minimum number of bytes using little-endian ordering.
// Negatives are encoded as absolute values with a high-order sign bit.
template <std::integral T>
MinimalIntEncoded<T> EncodeMinimalInt(T value) {
  MinimalIntEncoded<T> x;
  if (value == 0) return x;
  for (auto remainder = detail::Abs(value); remainder > 0; remainder >>= 8)
    x.Append(remainder & 0xFF);
  if (x.bytes[x.size - 1] & 0x80) x.Append(0);  // Adds a byte to disambiguate sign bit.
  if (value < 0) x.bytes[x.size - 1] |= 0x80;   // Sets the sign bit for negatives.
  return x;
}

template <typename T>
struct MinimalIntDecoded {
  operator T() const {
    return value;
  }
  T value;        // The decoded integer value.
  bool minimal;   // Whether the encoding was minimal.
  bool overflow;  // Whether overflow occurred during decoding.
};

// Decodes an integer from its minimal encoding.
template <std::signed_integral T>
MinimalIntDecoded<T> DecodeMinimalInt(std::span<const uint8_t> bytes) {
  using U = std::make_unsigned_t<T>;
  if (bytes.empty()) return {0, true, false};
  MinimalIntDecoded<T> result = {0, true, false};
  const int last = std::ssize(bytes) - 1;
  int pos = last;
  const bool negative = (bytes[pos] & 0x80) != 0;
  if ((bytes[pos] & 0x7F) == 0) {
    // The high byte exists for sign disambiguation only. Therefore another byte
    // must precede it with its high bit set.
    --pos;
    result.minimal = bytes.size() != 1 && (bytes[pos] & 0x80) != 0;
  }
  U absval = 0;
  for (; pos >= 0; --pos) {
    const uint8_t mask = pos == last ? 0x7F : 0xFF;  // Mask out the sign bit only.
    // Causes overflow if the high byte is non-zero.
    result.overflow |= (absval >> ((sizeof(U) - 1) << 3)) != 0;
    absval = (absval << 8) | (bytes[pos] & mask);
  }
  result.value = negative ? detail::Negate(absval) : T(absval);
  return result;
}

inline bool IsEncodedZero(std::span<const uint8_t> data) {
  bool zero = true;
  for (int i = 0; i < std::ssize(data); ++i) {
    const uint8_t nonsign_bits = i < std::ssize(data) - 1 ? data[i] : data[i] & 0x7F;
    if (!(zero &= nonsign_bits == 0)) break;
  }
  return zero;
}

}  // namespace hornet::protocol::script::lang
