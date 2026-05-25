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
  Nop = 0x61,
  If = 0x63,
  NotIf = 0x64,
  Else = 0x67,
  EndIf = 0x68,
  Verify = 0x69,
  Return = 0x6a,
  
  // Stack operations.
  ToAltStack = 0x6b,
  FromAltStack = 0x6c,
  Drop2 = 0x6d,
  Duplicate2 = 0x6e,
  Duplicate3 = 0x6f,
  Over2 = 0x70,
  Rotate2 = 0x71,
  Swap2 = 0x72,
  IfDup = 0x73,
  Depth = 0x74,
  Drop = 0x75,
  Pop = Drop,
  Duplicate = 0x76,
  Nip = 0x77,
  Over = 0x78,
  Pick = 0x79,
  Roll = 0x7a,
  Rotate = 0x7b,
  Swap = 0x7c,
  Tuck = 0x7d,

  // Bitwise operations.
  Equal = 0x87,
  EqualVerify = 0x88,

  // Arithmetic operations.
  Increment = 0x8b,
  Decrement = 0x8c,
  Negate =  0x8f,
  Abs = 0x90,
  IsZero = 0x91,
  IsNonZero = 0x92,
  Add = 0x93,
  Subtract = 0x94,
  BooleanAnd = 0x9a,
  BooleanOr = 0x9b,
  NumEqual = 0x9c,
  NumEqualVerify = 0x9d,
  NumNotEqual = 0x9e,
  LessThan = 0x9f,
  GreaterThan = 0xa0,
  LessThanOrEqual = 0xa1,
  GreaterThanOrEqual = 0xa2,
  Min = 0xa3,
  Max = 0xa4,

  // Cryptographic operations.
  RIPEMD160 = 0xa6,
  SHA1 = 0xa7,
  SHA256 = 0xa8,
  Hash160 = 0xa9,
  Hash256 = 0xaa,
  CodeSeparator = 0xab,
  CheckSig = 0xac,
  CheckSigVerify = 0xad,
  CheckMultiSig = 0xae,
  CheckMultiSigVerify = 0xaf,

  // Later operations.
  CheckLockTimeVerify = 0xb1,
  CheckSequenceVerify = 0xb2,
  CheckSigAdd = 0xba,

  // Upgradeable operations
  Nop1 = 0xb0,
  Nop4 = 0xb3,
  Nop5 = 0xb4,
  Nop6 = 0xb5,
  Nop7 = 0xb6,
  Nop8 = 0xb7,
  Nop9 = 0xb8,
  Nop10 = 0xb9
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

inline constexpr bool IsConditional(Op opcode) {
  return opcode >= Op::If && opcode <= Op::EndIf;
}

}  // namespace hornet::protocol::script::lang
