#pragma once

#include "io_stream.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

/*
    NOTES
    =====

    On the subject of the layers for (de)serialization:

    The goal at this layer is not to overconstrain with concrete types, but
    to allow protocol encodings to general stream-like objects through templated
    Reader/Writer types in the encoding/decoding layer. The functions
        void Write(Writer&, const char*, size_t), and
        void Read(Reader&, char* size_t)
    "just" need to find their correct "overrides", whether that be through
    template function specialization or regular function overloading.

    This allows a different layer of the library code to provide implementations
    for std::istream/ostream, which should then be automatically called for classes
    like MemoryIStream/OStream which derive from them.

    In addition, library users may define their own Reader/Writer types and write
    their own overloads for Read/Write to supply their own behavior.

    However, this is not entirely trivial to organize automagically, as there is a
    tendency for templates to get instantiated too early, leading to unintended
    resolution. And some subtleties for library users as to whether to write
    function specializations or function overloads, and in what namespace.

    The different rules over qualified vs unqualified namespaces only complicate
    the setup further.

    It is possible that an elegant instance of the Customization Point Object (CPO)
    idiom might neatly resolve these subtleties to make a very nice, extensible,
    and foolproof pattern for customizable generality and layered implementations.
    However, at this point, we have shelved that particular exploration in favor
    of a simiple and practical temporary measure.

    That temporary measure is to simply include the iostream versions of Read/Write
    for now, and treat them as concrete types, and to use MemoryI/OStream classes to
    ensure that can be true for now. Thus, we have hiacked the templated extensibility
    for now in order to prioritize protocol progress, but this can be further
    developed at a later time.
*/
// template <typename Writer> void Write(Writer& os, const char* data, size_t size);
// template <typename Reader> void Read(Reader&, char*, size_t);


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
