// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <span>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/protocol/script/instruction.h"
#include "hornetlib/protocol/script/op.h"

namespace hornet::protocol::script {

class Parser {
 public:
  using Iterator = std::span<const uint8_t>::iterator;

  Parser(std::span<const uint8_t> bytes) : cursor_(bytes.begin()), end_(bytes.end()) {}
  Parser(const Parser&) = default;

  bool IsEof() const {
    return cursor_ >= end_;
  }
  Iterator GetPos() const {
    return cursor_;
  }

  void GotoEof() {
    cursor_ = end_;
  }

  std::optional<Instruction> Next() {
    if (IsEof()) return std::nullopt;

    auto start = cursor_;
    const Op opcode = static_cast<Op>(*start++);
    const auto size = ReadPushSize(opcode, start);
    if (!size || start + size->variable_size + size->length_bytes > end_) {
      GotoEof();
      return std::nullopt;
    }
    start += size->variable_size;
    cursor_ = start + size->length_bytes;
    return Instruction{opcode, {&*start, size->length_bytes}};
  }

  std::optional<Op> Peek() const {
    if (IsEof()) return std::nullopt;
    return static_cast<Op>(*cursor_);
  }

 private:
  struct PushSize {
    uint8_t variable_size;
    uint32_t length_bytes;
  };

  std::optional<PushSize> ReadPushSize(Op opcode, Iterator start) const {
    if (opcode < Op::PushData1)
      return PushSize{0, ToByte(opcode)};
    else if (opcode == Op::PushData1)
      return ReadPushSizeVariable<uint8_t>(start);
    else if (opcode == Op::PushData2)
      return ReadPushSizeVariable<uint16_t>(start);
    else if (opcode == Op::PushData4)
      return ReadPushSizeVariable<uint32_t>(start);
    return PushSize{0, 0};
  }

  template <std::unsigned_integral T>
  std::optional<PushSize> ReadPushSizeVariable(Iterator start) const {
    if (start + sizeof(T) > end_)
      return std::nullopt;
    encoding::Reader reader({&*start, static_cast<size_t>(end_ - start)});
    return PushSize{sizeof(T), reader.ReadLE<T>()};
  }
  
  Iterator cursor_;
  Iterator end_;
};

}  // namespace hornet::protocol::script
