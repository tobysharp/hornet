// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <ostream>
#include <string>
#include <sstream>
#include <stdexcept>

#include "hornetlib/util/log.h"
#include "hornetlib/util/assert.h"

namespace hornet::util {

template <typename... Args>
[[nodiscard]] inline std::string ToString(const Args&... args) {
    std::ostringstream oss;
    if constexpr (sizeof...(args) > 0)
        (oss << ... << args);  // Fold expression (C++17)
    return oss.str();
}

template <typename... Args>
[[noreturn]] inline void ThrowRuntimeError(const Args&... args) {
    const auto str = ToString(args...);
    LogError("Throwing runtime error: ", str);
    throw std::runtime_error{str};
}

template <typename... Args>
[[noreturn]] inline void ThrowOutOfRange(const Args&... args) {
    const auto str = ToString(args...);
    LogError("Throwing out-of-range error: ", str);    
    throw std::out_of_range{str};
}

template <typename... Args>
[[noreturn]] inline void ThrowInvalidArgument(const Args&... args) {
    const auto str = ToString(args...);
    LogError("Throwing invalid-argument error: ", str);
    throw std::invalid_argument{str};
}

template <typename... Args>
[[noreturn]] inline void ThrowLogicError(const Args&... args) {
    const auto str = ToString(args...);
    LogError("Throwing logic error: ", str);
    throw std::logic_error{str};
}

}  // namespace hornet::util
