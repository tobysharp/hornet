#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>

using uint256_t = std::array<uint32_t, 8>;

// Writes the uint256_t as a 64-character hex string to an output stream,
// using big-endian byte order (as typically displayed in Bitcoin).
// Note: this is a textual representation, not binary output.
// Use std::ostream::write or serialization methods for raw binary encoding.
inline std::ostream& operator <<(std::ostream& os, const uint256_t& h)
{
    std::ios_base::fmtflags f(os.flags());  // Save stream flags
    os << std::hex << std::setfill('0');

    for (const uint32_t ui : h)
        os << std::setw(8) << ui;

    os.flags(f); // Restore flags
    return os;
}