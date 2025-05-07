#pragma once

#include "encoding/encoding.h"
#include "encoding/io.h"

template <>
struct EncodingTraits<encoding::LittleEndian> {
    template <typename T, typename Writer>
    static void Encode(Writer& os, const T& val)
    requires std::is_integral_v<T>
    {
        Write(os, val);
    }
};