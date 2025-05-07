#pragma once

#include "encoding/encoding.h"
#include "encoding/little_endian.h"
#include "protocol.h"

template <>
struct EncodingTraits<encoding::Default> {
    template <typename T, typename Writer>
    static void Encode(Writer& os, const T& val)
    requires std::is_integral_v<T>
    {
        EncodingTraits<encoding::LittleEndian>::Encode(os, val);
    }

    template <typename T, typename Reader>
    static void Decode(Reader& is, T& val)
    requires std::is_integral_v<T>
    {
        is >> As<encoding::LittleEndian>(val);
    }

    // Ports will always be encoded as big-endian 16-bit unsigned
    static void Encode(auto& os, const Port& p) {
        os << As<encoding::BigEndian>(p.value);
    }

    // Ports will always be encoded as big-endian 16-bit unsigned
    static void Decode(auto& is, Port& p) {
        is >> As<encoding::BigEndian>(p.value);
    }
};

