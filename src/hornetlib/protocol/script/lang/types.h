// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <numeric>
#include <span>

#include "hornetlib/protocol/script/lang/op.h"

namespace hornet::protocol::script::lang {

// The fundamental data type used as input and output for script operations.
using Bytes = std::span<const uint8_t>;

// A 160-bit binary field as a 20-byte array.
using Hash160 = std::array<uint8_t, 20>;

// A Bitcoin Script instruction.
struct Instruction {
  Op opcode;   // The opcode to be executed.
  Bytes data;  // The associated data argument for push instructions.
  Bytes raw;   // The raw bytes of the instruction.
};

// Reasons for Bitcoin Script failure.
enum class Error {
  NonMinimalNumber,     // An integer was not encoded minimally.
  NonMinimalPush,       // A push operation did not use the minimal opcode.
  NumberOverflow,       // An encoded integer was outside the permitted size range.
  StackItemOverflow,    // An item pushed to the stack was too large.
  StackOverflow,        // Too many items were pushed to the stack.
  StackUnderflow,       // An empty stack was popped.
  OpCountExcessive,     // Too many non-push operations were encountered in the script.
  InvalidSpendContext,  // Tried to access the spending context, but it doesn't exist.
  MalformedScript,      // Script not parseable as well-formed.
  OpVerify,             // An Op::Verify opcode failed.
  OpEqualVerify         // An Op::EqualVerify opcode failed.
};

// Returns false for empty, zero, and negative zero.
inline bool AsBool(Bytes data) {
  for (int i = 0; i < std::ssize(data); ++i)
    if (data[i] != 0) return i < std::ssize(data) - 1 || data[i] != 0x80;
  return false;
}

}  // namespace hornet::protocol::script::lang
