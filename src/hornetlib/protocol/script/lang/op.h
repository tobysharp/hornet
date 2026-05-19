// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include "hornetlib/util/assert.h"

namespace hornet::protocol::script::lang {

// The set of Bitcoin Script opcodes.
enum class Op : uint8_t {
  // Pushes nothing / empty / null / zero.
  PushEmpty = 0x00,        // Pushes the empty set.

  // Pushes arbitrary data.
  PushSize1 = 0x01,       // Pushes the next one byte of data.
  // ... contiguous up to ...
  PushSizeMax = 0x4b,     // Pushes the next 75 bytes of data. (= Push1 + 74)
  PushData1 = 0x4c,       // Pushes the data blob following its length, which is LE-encoded in the next one byte.
  PushData2 = 0x4d,       // Pushes the data blob following its length, which is LE-encoded in the next two bytes.
  PushData4 = 0x4e,       // Pushes the data blob following its length, which is LE-encoded in the next four bytes.

  // Pushes immediate integer constants.
  PushConstNegative1 = 0x4f,  // Pushes the immediate integer -1.
  PushConst0 = PushEmpty,     // Pushes the immediate value zero.
  PushConst1 = 0x51,          // Pushes the immediate integer 1.
  // ... contiguous up to ...
  PushConst16 = 0x60,         // Pushes the immediate integer 16.
  PushConstMin = PushConstNegative1,
  PushConstMax = PushConst16,

  // Pushes immediate Boolean constants.
  PushFalse = PushConst0,     // Pushes the immediate Boolean FALSE.
  PushTrue = PushConst1,      // Pushes the immediate Boolean TRUE.

  // Control operations.
  Verify = 0x69,
  Return = 0x6a,
  
  // Stack operations.
  Drop = 0x75,
  Pop = Drop,
  Duplicate = 0x76,

  // Bitwise operations.
  Equal = 0x87,
  EqualVerify = 0x88,

  // Arithmetic operations.
  Add = 0x93,

  // Cryptographic operations.
  Hash160 = 0xa9,
  CodeSeparator = 0xab,

  // Check signature opcodes.
  CheckSig = 0xac,
  CheckSigVerify = 0xad,
  CheckMultiSig = 0xae,
  CheckMultiSigVerify = 0xaf
};

inline constexpr int OpCount = 256;

inline constexpr int kPushConstantMin = -1;
inline constexpr int kPushConstantMax = 16;

inline constexpr uint8_t operator +(Op op) {
  return uint8_t(op);
}

inline constexpr uint8_t ToByte(Op op) { 
  return uint8_t(op);
}

inline constexpr std::strong_ordering operator <=>(Op lhs, Op rhs) {
  return ToByte(lhs) <=> ToByte(rhs);
}

inline constexpr int operator -(Op lhs, Op rhs) {
  return ToByte(lhs) - ToByte(rhs);
}

inline constexpr Op operator +(Op lhs, int rhs) {
  return Op(ToByte(lhs) + rhs);
}

inline constexpr Op& operator++(Op& op) {
  return op = op + 1;
}

inline constexpr bool IsConstantPushable(int value) {
  return value >= kPushConstantMin && value <= kPushConstantMax;
}

inline constexpr bool IsConstantPush(Op opcode) {
  return opcode == Op::PushConst0 || (opcode >= Op::PushConst1 && opcode <= Op::PushConstMax) || opcode == Op::PushConstNegative1;
}

inline constexpr Op ConstantToOp(int value) {
  Assert(IsConstantPushable(value));
  return value == 0 ? Op::PushConst0 : (Op::PushConstMin + (value - kPushConstantMin));
}

inline constexpr int OpToConstant(Op opcode) {
  Assert(IsConstantPush(opcode));
  return opcode == Op::PushConst0 ? 0 : (kPushConstantMin + (opcode - Op::PushConstMin));
}

inline constexpr bool IsPush(Op opcode) {
  return opcode <= Op::PushConstMax;
}

inline constexpr bool IsDirectPush(Op opcode) {
  return opcode >= Op::PushSize1 && opcode <= Op::PushSizeMax;
}

inline constexpr int DirectPushSize(Op opcode) {
  Assert(IsDirectPush(opcode));
  return +opcode;
}

}  // namespace hornet::protocol::script::lang
