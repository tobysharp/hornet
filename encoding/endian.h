#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "util/throw.h"

namespace hornet::encoding {

// Returns true (at compile time) when targeting little-endian systems
inline constexpr bool IsLittleEndian() {
  return std::endian::native == std::endian::little;
}

template <std::integral T>
inline T SwapEndian(const T value) {
  T result;
  const uint8_t *src = reinterpret_cast<const uint8_t *>(&value);
  uint8_t *dst = reinterpret_cast<uint8_t *>(&result);
  std::reverse_copy(src, src + sizeof(T), dst);
  return result;
}

template <std::integral T>
inline T NativeToLittleEndian(const T native) {
  return IsLittleEndian() ? native : SwapEndian(native);
}

template <std::integral T>
inline T NativeToBigEndian(const T native) {
  return IsLittleEndian() ? SwapEndian(native) : native;
}

template <std::integral T>
inline T LittleEndianToNative(const T le) {
  return NativeToLittleEndian(le);
}

template <std::integral T>
inline T BigEndianToNative(const T le) {
  return NativeToBigEndian(le);
}

template <std::integral To, std::integral From>
To NarrowOrThrow(From value) {
  if (value > std::numeric_limits<To>::max())
    util::ThrowOutOfRange("Data lost in type conversion of value ", value, ".");

  return static_cast<To>(value);
}

}  // namespace hornet::encoding