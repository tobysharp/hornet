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

using Bytes = std::span<const uint8_t>;

struct Instruction {
  Op opcode;
  Bytes data;
  int offset;
};

enum Mode { Legacy = 0, Segwit = 1, Tapscript = 2, Count };

enum class Error {
  NonMinimalNumber,
  NonMinimalPushOp,
  NumberOverflow,
  StackItemOverflow,
  StackOverflow,
  StackEmpty,
  OpCountExcessive
};

inline int MaxNonPushOps(Mode mode) {
  return mode == Mode::Legacy || mode == Mode::Segwit ? 201 : std::numeric_limits<int>::max();
}

}  // namespace hornet::protocol::script::lang
