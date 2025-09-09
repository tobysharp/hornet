// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <expected>
#include <optional>

#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/runtime/decode.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/log.h"

namespace hornet::protocol::script {

Processor::Processor(std::span<const uint8_t> script,
                    bool require_minimal,
                    int height
                    )
    : parser_(script),
      policy_{require_minimal},
      env_{height, runtime::Version::Legacy},
      machine_(runtime::Machine{.stack = stack_, .script = parser_.Script(), .policy = policy_}) {
}

std::optional<int32_t> Processor::TryPeekInt() const {
  if (stack_.Empty()) return std::nullopt;
  try {
    return runtime::DecodeInt32(stack_.Top(), policy_.require_minimal);
  } catch (const runtime::Exception&) {
    return std::nullopt;
  }
}

// Step execution forward by one instruction.
util::Expected<bool, lang::Error> Processor::Step() {
  if (error_) return *error_;  // Execution already faulted, must reset.

  try {
    if (const auto instruction = parser_.Next()) Execute(*instruction);
    return !IsFinished();
  } catch (const runtime::Exception& e) {
    error_ = e.GetError();
    LogWarn() << "Script execution error code " << int(*error_) << ": " << e.what();
    return *error_;
  }
}

void Processor::Reset(std::span<const uint8_t> script, int height) {
  parser_ = {script};
  error_.reset();
  stack_.Clear();
  env_ = { height };
  machine_.emplace(runtime::Machine{.stack = stack_, .script = script, .policy = policy_});
}

// Run the script to the end and return its Boolean result.
util::Expected<bool, lang::Error> Processor::Run() {
  if (error_) return *error_;  // Execution already faulted, must reset.

  try {
    while (const auto instruction = parser_.Next()) Execute(*instruction);
  } catch (const runtime::Exception& e) {
    error_ = e.GetError();
    LogWarn() << "Script execution error code " << int(*error_) << ": " << e.what();
    return *error_;
  }
  return PeekBool();
}

void Processor::Execute(const lang::Instruction& instruction) {
  Assert(!error_);
  runtime::StepExecution(runtime::Context{env_, *machine_, instruction});
}

}  // namespace hornet::protocol::script
