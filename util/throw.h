#pragma once

#include <ostream>
#include <string>
#include <stdexcept>

#include "util/assert.h"

namespace hornet::util {

template <typename... Args>
inline std::string ToString(const Args&... args) {
    std::ostringstream oss;
    (oss << ... << args);  // Fold expression (C++17)
    return oss.str();
}

template <typename... Args>
[[noreturn]] inline void ThrowRuntimeError(const Args&... args) {
    Assert(false);
    throw std::runtime_error{ToString(args...)};
}

template <typename... Args>
[[noreturn]] inline void ThrowOutOfRange(const Args&... args) {
    Assert(false);
    throw std::out_of_range{ToString(args...)};
}

template <typename... Args>
[[noreturn]] inline void ThrowInvalidArgument(const Args&... args) {
    Assert(false);
    throw std::invalid_argument{ToString(args...)};
}

}  // namespace hornet::util
