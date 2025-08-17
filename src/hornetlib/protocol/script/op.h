// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <compare>
#include <cstdint>

namespace hornet::protocol::script {

enum class Op : uint8_t {
  Push0 = 0x00,
  False = Push0,
  PushData1 = 0x4c,
  PushData2 = 0x4d,
  PushData4 = 0x4e
};

inline constexpr uint8_t ToByte(Op op) { 
  return static_cast<uint8_t>(op); 
}

inline constexpr std::strong_ordering operator <=>(Op lhs, Op rhs) {
  return ToByte(lhs) <=> ToByte(rhs);
}

inline int operator -(Op lhs, Op rhs) {
  return ToByte(lhs) - ToByte(rhs);
}

}  // namespace hornet::protocol::script
