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

// Returns the maximum permitted number of non-push operations during a script, depending on script
// version.
static int MaxNonPushOps(runtime::Version version) {
  static constexpr int kMaxNonPushOps = 201;
  return version == runtime::Version::Legacy || version == runtime::Version::SegwitV0 ? kMaxNonPushOps
                                                                                      : std::numeric_limits<int>::max();
}

Processor::Processor(const runtime::Policy& policy /* = {} */,
                     // int height /* = 0 */,
                     std::optional<SpendContext> spend /* = std::nullopt */
                     )
    : policy_{policy},
      env_{/*height, */ runtime::Version::Legacy, std::move(spend)},
      machine_(runtime::Machine{.stack = stack_,
                                .altstack = altstack_,
                                .conditions = conditions_,
                                .max_non_push_ops = MaxNonPushOps(env_.version),
                                .script = parser_.Script(),
                                .policy = policy_}) {}

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
    bool more_instructions = parser_.Peek().has_value();
    if (!more_instructions) {
      if (!parser_.IsEof()) return lang::Error::MalformedScript;
      if (!conditions_.Empty()) return lang::Error::UnbalancedCondition;
    }
    return more_instructions;
  } catch (const runtime::Exception& e) {
    error_ = e.GetError();
    LogWarn() << "Script execution error code " << int(*error_) << ": " << e.what();
    return *error_;
  }
}

void Processor::Reset(std::span<const uint8_t> script) {
  parser_ = {script};
  error_ = {};
  conditions_ = {};
  altstack_ = {};
  machine_.emplace(runtime::Machine{.stack = stack_,
                                    .altstack = altstack_,
                                    .conditions = conditions_,
                                    .max_non_push_ops = MaxNonPushOps(env_.version),
                                    .script = script,
                                    .policy = policy_});
}

// Run the script to the end and return its Boolean result.
util::Expected<bool, lang::Error> Processor::Run() {
  if (error_) return *error_;  // Execution already faulted, must reset.

  try {
    while (const auto instruction = parser_.Next()) Execute(*instruction);
    if (!parser_.IsEof()) return lang::Error::MalformedScript;
    if (!conditions_.Empty()) return lang::Error::UnbalancedCondition;
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
