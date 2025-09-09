// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>
#include <span>

#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/util/expected.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol::script {

class Processor {
 public:
  explicit Processor(std::span<const uint8_t> script,
                     bool require_minimal = true,  // Becomes execution policy
                     int height = 0                // Becomes environment context
                     );

  void Reset(std::span<const uint8_t> script, int height);

  // Runs until completion and returns the Boolean interpretation of the top-of-stack (or error).                     
  util::Expected<bool, lang::Error> Run();

  // Executes the next instruction and returns true iff it's possible to Step() again (or error).
  util::Expected<bool, lang::Error> Step();

  bool IsFinished() const { return !parser_.Peek(); }

  bool PeekBool() const { return !stack_.Empty() && stack_.TopAsBool(); }

  // Try to interpret the top-of-stack as a 32-bit signed integer, if valid.
  std::optional<int32_t> TryPeekInt() const;

  std::optional<lang::Bytes> TryPeek() const {
    if (stack_.Empty()) return std::nullopt;
    return stack_.Top();
  }

  std::optional<lang::Error> LastError() const { return error_; }

  const Parser& Parser() const {
    return parser_;
  }

 private:
  void Execute(const lang::Instruction& instruction);

  script::Parser parser_;
  runtime::Policy policy_;
  runtime::Environment env_;
  runtime::Stack stack_;
  std::optional<runtime::Machine> machine_;
  std::optional<lang::Error> error_;
};

}  // namespace hornet::protocol::script
