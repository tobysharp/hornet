#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <span>

using bytes32_t = std::array<uint8_t, 32>;

template <typename T>
inline std::span<const uint8_t> AsByteSpan(std::span<const T> input) {
  static_assert(std::is_trivially_copyable_v<T>,
                "AsByteSpan requires trivially copyable types");
  return {reinterpret_cast<const uint8_t*>(input.data()), input.size_bytes()};
}

// Writes the uint256_t as a 64-character hex string to an output stream,
// using big-endian byte order (as typically displayed in Bitcoin).
// Note: this is a textual representation, not binary output.
// Use std::ostream::write or serialization methods for raw binary encoding.
inline std::ostream& operator <<(std::ostream& os, const bytes32_t& h)
{
    std::ios_base::fmtflags f(os.flags());  // Save stream flags
    os << std::hex << std::setfill('0');

    for (const uint8_t uc : h)
        os << std::setw(2) << static_cast<int>(uc);

    os.flags(f); // Restore flags
    return os;
}