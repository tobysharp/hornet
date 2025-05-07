#pragma once

#include "encoding/io.h"

// Encoding tags
namespace encoding {
    struct VarInt{};
    struct VarString{};
    struct LittleEndian{}; // needed?
    struct BigEndian{};
    struct RawBytes{};
    struct Default{};
}

template <typename Encoding, typename T>
class Wrapper {
public:
    // Used by concept HasEncoding to detect wrapper types
    using IsWrapperType = void;

    Wrapper(T& v) : value_(v) {}
    T& operator *() { return value_; }
private:
    T& value_; // Or T?
};

// The HasEncoding concept will be used to distinguish between types that are wrapper in
// an encoding semantic, and types that are purely data without encoding tags.
template <typename T, typename = void> struct HasEncoding : std::false_type {};
template <typename T> struct HasEncoding<T, std::void_t<typename T::IsWrapperType>> : std::true_type {};

template <typename Encoding, typename T>
Wrapper<Encoding, T> As(T& ref) {
    return ref;
}

template <typename Encoding>
struct EncodingTraits;

// Stream operator << for types wrapped with a specific encoding
template <typename Encoding, typename T, typename Writer>
Writer& operator<<(Writer& os, Wrapper<Encoding, const T> ref) {
    EncodingTraits<Encoding>::template Encode<T>(os, *ref);
    return os;
}

// Stream operator >> for types wrapped with a specific encoding
template <typename Encoding, typename T, typename Reader>
Reader& operator>>(Reader& is, Wrapper<Encoding, T> ref) {
    *ref = EncodingTraits<Encoding>::template Decode<T>(is);
    return is;
}
