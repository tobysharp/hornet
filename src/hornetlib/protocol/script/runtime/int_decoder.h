#pragma once

#include <concepts>
#include <cstdint>
#include <span>

#include "hornetlib/protocol/script/common/minimal.h"
#include "hornetlib/protocol/script/runtime/exception.h"

namespace hornet::protocol::script::runtime {

class MinimalIntDecoder {
 public:
  // When set, causes DecodeX to throw an exception if it is passed a non-minimally encoded buffer.
  void SetRequireMinimal(bool set = true) {
    is_minimal_required_ = set;
  }

  // Decodes up to 4 bytes and return the result in a 32-bit signed integer.
  int32_t Decode4(std::span<const uint8_t> bytes) const {
    return Decode<int32_t, 4>(bytes);
  }

  // Decodes up to 5 bytes and return the result in a 64-bit signed integer.
  int64_t Decode5(std::span<const uint8_t> bytes) const {
    return Decode<int64_t, 5>(bytes);
  }

 private:
  template <std::signed_integral T, int kMaxNumBytes>
  T Decode(std::span<const uint8_t> bytes) const {
    if (std::ssize(bytes) > kMaxNumBytes)
      Throw("Could not decode a buffer of size ", bytes.size(), " bytes (max ", kMaxNumBytes, ").");
    const auto decoded = common::DecodeMinimalInt<T>(bytes);
    if (is_minimal_required_ && !decoded.minimal)
      Throw("Value ", decoded.value, " was not minimally encoded.");
    Assert(!decoded.overflow);
    return decoded.value;
  }

  bool is_minimal_required_ = false;
};

}  // namespace hornet::protocol::script::runtime
