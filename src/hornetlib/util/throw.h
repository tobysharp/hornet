// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <ostream>
#include <string>
#include <sstream>
#include <stdexcept>

#include "hornetlib/util/assert.h"

namespace hornet::util {

template <typename... Args>
inline std::string ToString(const Args&... args) {
    std::ostringstream oss;
    (oss << ... << args);  // Fold expression (C++17)
    return oss.str();
}

template <typename... Args>
[[noreturn]] inline void ThrowRuntimeError(const Args&... args) {
    throw std::runtime_error{ToString(args...)};
}

template <typename... Args>
[[noreturn]] inline void ThrowOutOfRange(const Args&... args) {
    throw std::out_of_range{ToString(args...)};
}

template <typename... Args>
[[noreturn]] inline void ThrowInvalidArgument(const Args&... args) {
    throw std::invalid_argument{ToString(args...)};
}

}  // namespace hornet::util
