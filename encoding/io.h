#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>

#include <vector>

template <typename Writer> void Write(Writer& os, const char* data, size_t size);
template <typename Reader> void Read(Reader&, char*, size_t);

// Writes a simple type to the output stream in little-endian format
template <typename T, typename Writer>
inline void Write(Writer& os, T value) {
    static_assert(std::is_integral_v<T>, "Write<T> requires an integral type.");
    Write(os, reinterpret_cast<const char*>(&value), sizeof(T));
}

// Reads an integer of type T from the input stream.
template <typename T, typename Reader> 
inline T Read(Reader& is) {
    static_assert(std::is_integral_v<T>, "Read<T> requires an integral type.");
    T read = 0;
    Read(is, reinterpret_cast<char*>(&read), sizeof(read));
    return read;;
}

// Reads an integer of type U from the input stream, and checks whether
// it will fit in a variable of type T. If so, then it returns the T; otherwise
// it throws an exception.
template <typename T, typename U = std::make_unsigned_t<T>, typename Reader> 
inline T ReadCast(Reader& is) {
    static_assert(std::is_integral_v<T> && std::is_integral_v<U>, 
        "Read<T, U> requires integral types.");

    U read = Read<U>(is);
    if constexpr (!std::is_same_v<T, U>) {
        // If T is a smaller type than U, then the value read may not fit
        // inside a T without loss. In this case, we throw an exception.
        if (!std::in_range<T>(read))
            throw std::range_error{"ReadCast couldn't perform a type conversion without data loss."};
    }
    // Otherwise it is safe to cast to type T.
    return static_cast<T>(read);
}
