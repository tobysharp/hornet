#pragma once

#include "encoding/encoding.h"
#include "encoding/io.h"

template <>
struct EncodingTraits<Encoding::kVarInt> {
// Writes an unsigned integer to the stream with a variable-sized encoding
    template <typename T, typename Writer>
    static void Encode(Writer& os, T value) {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);

        if (value <= 0xFC) {
            Write(os, static_cast<uint8_t>(value));
        } else if (value <= 0xFFFF) {
            Write(os, uint8_t{0xFD});
            Write(os, static_cast<uint16_t>(value));
        } else if (value <= 0xFFFFFFFF) {
            Write(os, uint8_t{0xFE});
            Write(os, static_cast<uint32_t>(value));
        } else {
            Write(os, uint8_t{0xFF});
            Write(os, static_cast<uint64_t>(value));
        }
    }

    // Reads an integer from the stream with a variable-sized encoding.
    // Throws an exception if the requested type is too small.
    template <typename T, typename Reader>
    static T Decode(Reader& is) {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
    
        const uint8_t prefix = Read<uint8_t>(is);
        if (prefix < 0xFD) {
            return static_cast<T>(prefix);
        } else if (prefix == 0xFD) {
            return ReadCast<T, uint16_t>(is);   
        } else if (prefix == 0xFE) {
            return ReadCast<T, uint32_t>(is);
        } else {
            return ReadCast<T, uint64_t>(is);
        }
    }
};

template <typename T>
Wrapper<Encoding::kVarInt, T> AsVarInt(T& value) {
    static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
    return value;
}
