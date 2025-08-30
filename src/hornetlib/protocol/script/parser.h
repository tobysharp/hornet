// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <span>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"

namespace hornet::protocol::script {

class Parser {
 public:
  using Iterator = lang::Bytes::iterator;

  Parser(lang::Bytes bytes) : script_(bytes), cursor_(bytes.begin()) {}

  std::optional<lang::Instruction> Next() {
    if (cursor_ >= script_.end()) return std::nullopt;

    const auto it_opcode = cursor_;
    const auto opcode = lang::Op(*it_opcode);
    const auto it_pushdata = it_opcode + 1;
    const auto size = ReadInstructionSize(opcode, it_pushdata);
    if (!size || it_pushdata + size->pushdata_bytes + size->payload_bytes > script_.end()) {
      cursor_ = script_.end();
      return std::nullopt;
    }
    const auto it_payload = it_pushdata + size->pushdata_bytes;
    cursor_ = it_payload + size->payload_bytes;
    return lang::Instruction{.opcode = opcode, 
      .data = {size->payload_bytes > 0 ? &*it_payload : nullptr, size->payload_bytes},
      .offset = int(it_opcode - script_.begin())};
  }

  std::optional<lang::Op> Peek() const {
    if (cursor_ >= script_.end()) return std::nullopt;
    return static_cast<lang::Op>(*cursor_);
  }

  lang::Bytes Script() const {
    return script_;
  }

 private:
  struct InstructionSize {
    uint8_t pushdata_bytes;
    uint32_t payload_bytes;
  };

  std::optional<InstructionSize> ReadInstructionSize(lang::Op opcode, Iterator pushdata) const {
    if (opcode < lang::Op::PushData1)
      return InstructionSize{0, ToByte(opcode)};
    else if (opcode == lang::Op::PushData1)
      return ReadPushSizeVariable<uint8_t>(pushdata);
    else if (opcode == lang::Op::PushData2)
      return ReadPushSizeVariable<uint16_t>(pushdata);
    else if (opcode == lang::Op::PushData4)
      return ReadPushSizeVariable<uint32_t>(pushdata);
    return InstructionSize{0, 0};
  }

  template <std::unsigned_integral T>
  std::optional<InstructionSize> ReadPushSizeVariable(Iterator pushdata) const {
    if (pushdata + sizeof(T) > script_.end()) return std::nullopt;
    encoding::Reader reader({&*pushdata, static_cast<size_t>(script_.end() - pushdata)});
    return InstructionSize{sizeof(T), reader.ReadLE<T>()};
  }

  lang::Bytes script_;
  Iterator cursor_;
};

}  // namespace hornet::protocol
