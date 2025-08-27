#pragma once

#include <array>
#include <concepts>
#include <numeric>
#include <optional>
#include <span>

#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

namespace hornet::protocol::script {

namespace detail {

// Returns the negative of an unsigned input.
// Safe for all input values up to 0x80...00.
template <std::unsigned_integral U>
inline constexpr auto Negate(U value) noexcept {
  using T = std::make_signed_t<U>;
  return T(~value + 1);
}

// Returns the unsigned absolute value of any integer.
// Safe for extreme values, e.g. Abs : (int8_t)0x80 --> (uint8_t)0x80.
template <std::integral T>
inline constexpr auto Abs(T value) noexcept {
  using U = std::make_unsigned_t<T>;
  // Uses twos-complement conversion to avoid UB on INT_MIN.
  return value < T{0} ? ~U(value) + 1 : U(value);
}
}  // namespace detail

template <std::integral T>
struct MinimalIntEncoded {
  void Add(uint8_t value) {
    Assert(size < std::ssize(bytes));
    bytes[size++] = value;
  }
  operator std::span<const uint8_t>() const {
    return {&bytes[0], size};
  }
  std::array<uint8_t, sizeof(T) + 1> bytes;
  int size = 0;
};

// Encodes the integer in the minimum number of bytes using little-endian ordering.
// Negatives are encoded as absolute values with a high-order sign bit.
template <std::integral T>
MinimalIntEncoded<T> EncodeMinimalInt(T value) {
  MinimalIntEncoded<T> x;
  if (value == 0) return x;
  for (auto remainder = detail::Abs(value); remainder > 0; remainder >>= 8) x.Add(remainder & 0xFF);
  if (x.bytes[x.size - 1] & 0x80) x.Add(0);    // Adds a byte to disambiguate sign bit.
  if (value < 0) x.bytes[x.size - 1] |= 0x80;  // Sets the sign bit for negatives.
  return x;
}

template <typename T>
struct MinimalIntDecoded {
  T value;
  bool minimal;
};

template <std::signed_integral T>
MinimalIntDecoded<T> DecodeMinimalInt(std::span<const uint8_t> bytes) {
  using U = std::make_unsigned_t<T>;
  if (bytes.empty()) return {0, true};
  MinimalIntDecoded<T> result = {0, true};
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
    absval = (absval << 8) | (bytes[pos] & mask);
  }
  result.value = negative ? detail::Negate(absval) : T(absval);
  return result;
}

}  // namespace hornet::protocol::script

