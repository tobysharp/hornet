#pragma once

#include <concepts>

namespace hornet::util {

// Safe for inputs up to 0x80...
template <std::unsigned_integral U>
constexpr auto Negate(U value) {
   using T = std::make_signed_t<U>;
   if (value > U(std::numeric_limits<T>::max()) + 1)
      ThrowOutOfRange("Cannot negate an unsigned value above INT_MAX+1.");
   return T(~value + 1);
}

// Returns the unsigned absolute value of any integer.
// Safe for extreme values, e.g. Abs : (int8_t)0x80 --> (uint8_t)0x80.
template <std::signed_integral T>
constexpr auto Abs(T value) noexcept {
   using U = std::make_unsigned_t<T>;
   // Uses twos-complement conversion to avoid UB on INT_MIN.
   return value < 0 ? ~U(value) + 1 : U(value);
}

}  // namespace hornet::util
