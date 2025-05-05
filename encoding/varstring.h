#pragma once

#include "encoding/encoding.h"

#include <iostream>

template <>
struct EncodingTraits<Encoding::kVarString> {
    static Write(std::ostream& os, const std::string& data) {
        EncodingTraits<Encoding::kVarInt>::Write(os, data.size());
        os.write(data.data(), data.size());
    }         

    static std::string Read(std::istream& is) {
        size_t size = EncodingTraits<Encoding::kVarInt>::Read(is);
        std::string data(size, '\0');
        is.read(data.data(), data.size());
        return data;
    }
};

template <typename T>
Wrapper<Encoding::kVarString, T> AsVarString(T& value) {
    static_assert(std::is_same_v<std::remove_cvref_t<T>, std::string>);
    return Wrapper<Encoding::VarString, T>(value);
}
