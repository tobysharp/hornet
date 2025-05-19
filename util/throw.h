#pragma once

#include <ostream>
#include <string>
#include <stdexcept>

namespace hornet::util {

template <typename... Args>
inline std::string ToString(const Args&... args) {
    std::ostringstream oss;
    (oss << ... << args);  // Fold expression (C++17)
    return oss.str();
}

template <typename... Args>
inline void ThrowRuntimeError(const Args&... args) {
    throw std::runtime_error{ToString(args...)};
}

template <typename... Args>
inline void ThrowOutOfRange(const Args&... args) {
    throw std::out_of_range{ToString(args...)};
}

}  // namespace hornet::util
