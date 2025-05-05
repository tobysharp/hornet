#pragma once

#include "encoding/io.h"

enum class Encoding {
    kVarInt,
    kVarString
};

template <Encoding kEncoding>
struct EncodingTraits;

template <Encoding E, typename T>
class Wrapper {
public:
    Wrapper(T& v) : value_(v) {}
    T& operator *() { return value_; }
private:
    T& value_;
};

template <Encoding kEncoding, typename T, typename Writer>
Writer& operator<<(Writer& os, Wrapper<kEncoding, T> ref) {
    if constexpr (requires { EncodingTraits<kEncoding>::template Encode<T>(os, ref); }) {
        EncodingTraits<kEncoding>::template Encode<T>(os, *ref);
    } else {
        EncodingTraits<kEncoding>::Encode(os, *ref);
    }
    return os;
}

template <Encoding kEncoding, typename T, typename Reader>
Reader& operator>>(Reader& is, Wrapper<kEncoding, T> ref) {
    if constexpr (requires { EncodingTraits<kEncoding>::template Decode<T>(is); }) {
        *ref = EncodingTraits<kEncoding>::template Decode<T>(is);
    } else {
        *ref = EncodingTraits<kEncoding>::Decode(is);
    }
    return is;
}
