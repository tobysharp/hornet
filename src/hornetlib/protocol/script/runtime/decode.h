// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <concepts>
#include <span>

#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

template <std::signed_integral T, int kMaxNumBytes>
inline T Decode(lang::Bytes bytes, bool require_minimal) {
  if (std::ssize(bytes) > kMaxNumBytes)
    Throw(lang::Error::NumberOverflow, "Could not decode a buffer of size ", bytes.size(), " bytes (max ", kMaxNumBytes, ").");
  const auto decoded = lang::DecodeMinimalInt<T>(bytes);
  if (require_minimal && !decoded.minimal)
    Throw(lang::Error::NonMinimalNumber, "Value ", decoded.value, " was not minimally encoded.");
  Assert(!decoded.overflow);
  return decoded.value;
}

// Decodes up to 4 bytes and return the result in a 32-bit signed integer.
inline int32_t DecodeInt32(lang::Bytes bytes, bool require_minimal = false) {
  return Decode<int32_t, 4>(bytes, require_minimal);
}

// Decodes up to 5 bytes and return the result in a 64-bit signed integer.
inline int64_t DecodeInt40(lang::Bytes bytes, bool require_minimal = false) {
  return Decode<int64_t, 5>(bytes, require_minimal);
}

}  // namespace hornet::protocol::script::runtime
