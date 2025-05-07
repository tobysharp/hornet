#pragma once

#include <bit>

// Returns true (at compile time) when targeting little-endian systems
inline constexpr bool IsLittleEndian() {
    return std::endian::native == std::endian::little;
}