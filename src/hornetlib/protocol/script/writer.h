// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <concepts>
#include <span>
#include <vector>

#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/util/abs.h"
#include "hornetlib/util/assert.h"

namespace hornet::protocol::script {

class Writer {
 public:
  operator std::span<const uint8_t>() const {
    return bytes_;
  }

  // Writes an instruction to push the given data onto the execution stack.
  Writer& PushData(std::span<const uint8_t> data) {
    using lang::Op;
    Assert(data.size() < 0xFFFFFFFF);
    if (data.empty())
      *this << Op::PushConst0;
    else if (data.size() <= ToByte(Op::PushSizeMax))
      *this << Op::PushSize1 + (data.size() - 1) << data;
    else if (data.size() <= 0xFF)
      *this << Op::PushData1 << uint8_t(data.size()) << data;
    else if (data.size() <= 0xFFFF)
      *this << Op::PushData2 << uint16_t(data.size()) << data;
    else
      *this << Op::PushData4 << uint32_t(data.size()) << data;
    return *this;
  }

  // Writes an instruction to push the given integer onto the execution stack.
  Writer& PushInt(int32_t value) {
    // If the value is in [-1, 16], push as immediate data in an opcode.
    if (lang::IsImmediate(value))
      *this << lang::ImmediateToOp(value);
    else {
      // Encodes the integer in the minimum number of bytes using little-endian ordering.
      // Negatives are encoded as absolute values with a high-order sign bit.
      PushData(lang::EncodeMinimalInt(value));
    }
    return *this;
  }

  Writer& Then(lang::Op opcode) {
    return *this << opcode;
  }

 private:
  // Writes a raw unsigned integer in little-endian encoding to the instruction stream.
  template <std::unsigned_integral T>
  Writer& operator<<(T value) {
    for (unsigned int i = 0; i < sizeof(T); ++i, value >>= 8) bytes_.push_back(value & 0xFF);
    return *this;
  }

  // Writes a raw byte to the instruction stream.
  Writer& operator<<(uint8_t value) {
    bytes_.push_back(value);
    return *this;
  }
  
  // Writes a one-byte opcode to the instruction stream.
  Writer& operator<<(lang::Op opcode) {
    return *this << ToByte(opcode);
  }

  // Writes a raw blob to the instruction stream.
  Writer& operator<<(std::span<const uint8_t> data) {
    bytes_.insert(bytes_.end(), data.begin(), data.end());
    return *this;
  }

  std::vector<uint8_t> bytes_;
};

}  // namespace hornet::protocol::script
