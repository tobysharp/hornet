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

  std::optional<Instruction> Next() {
    if (cursor_ >= end_) return std::nullopt;

    const auto it_opcode = cursor_;
    const Op opcode = static_cast<Op>(*it_opcode);
    const auto it_pushdata = it_opcode + 1;
    const auto size = ReadInstructionSize(opcode, it_pushdata);
    if (!size || it_pushdata + size->pushdata_bytes + size->payload_bytes > end_) {
      cursor_ = end_;
      return std::nullopt;
    }
    const auto it_payload = it_pushdata + size->pushdata_bytes;
    cursor_ = it_payload + size->payload_bytes;
    return Instruction{opcode, {size->payload_bytes > 0 ? &*it_payload : nullptr, size->payload_bytes}};
  }

  std::optional<Op> Peek() const {
    if (cursor_ >= end_) return std::nullopt;
    return static_cast<Op>(*cursor_);
  }

 private:
  struct InstructionSize {
    uint8_t pushdata_bytes;
    uint32_t payload_bytes;
  };

  std::optional<InstructionSize> ReadInstructionSize(Op opcode, Iterator pushdata) const {
    if (opcode < Op::PushData1)
      return InstructionSize{0, ToByte(opcode)};
    else if (opcode == Op::PushData1)
      return ReadPushSizeVariable<uint8_t>(pushdata);
    else if (opcode == Op::PushData2)
      return ReadPushSizeVariable<uint16_t>(pushdata);
    else if (opcode == Op::PushData4)
      return ReadPushSizeVariable<uint32_t>(pushdata);
    return InstructionSize{0, 0};
  }

  template <std::unsigned_integral T>
  std::optional<InstructionSize> ReadPushSizeVariable(Iterator pushdata) const {
    if (pushdata + sizeof(T) > end_) return std::nullopt;
    encoding::Reader reader({&*pushdata, static_cast<size_t>(end_ - pushdata)});
    return InstructionSize{sizeof(T), reader.ReadLE<T>()};
  }

  Iterator cursor_;
  Iterator end_;
};

}  // namespace hornet::protocol::script
