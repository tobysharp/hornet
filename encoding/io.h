#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>

// For the low-level read/write I/O operations, we use a tag-invoke Customization
// Point Object (CPO) pattern, which allows us to write this stream I/O code and 
// the encoding layer without defining a concrete read/write mechanism yet. Using
// tag-invoke, the read/write calls will be dispatched to the best available
// overload as late as possible. This means that users can define their own stream-
// like Read/Writer objects and provide their own streaming behavior using Dispatch
// overloads with the appropriate tag. These overloads do not need to appear in the
// io namespace.
namespace io {

inline constexpr struct WriteTag {
    template <typename Writer, typename... Args>
    auto operator()(Writer&& w, Args&&... args) const
    {
        return Dispatch(*this, std::forward<Writer>(w), std::forward<Args>(args)...);
    }
} WriteCPO;

inline constexpr struct ReadTag {
    template <typename Reader, typename... Args>
    auto operator()(Reader&& reader, Args&&... args) const {
        return Dispatch(*this, std::forward<Reader>(reader), std::forward<Args>(args)...);
    }
} ReadCPO;

}  // namespace io
    

// Writes a simple type to the output stream in little-endian format
template <typename T>
inline void Write(std::ostream& os, T value) {
    static_assert(std::is_integral_v<T>, "Write<T> requires an integral type.");
    WriteCPO(os, reinterpret_cast<const char*>(&value), sizeof(T));
}

// Reads an integer of type T from the input stream.
template <typename T, typename Reader> 
inline T Read(Reader& is) {
    static_assert(std::is_integral_v<T>, "Read<T> requires an integral type.");
    T read = 0;
    io::ReadCPO(is, reinterpret_cast<char*>(&read), sizeof(read));
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
