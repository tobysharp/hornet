#include <expected>
#include <optional>

#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/runtime/decode.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/assert.h"

namespace hornet::protocol::script {

std::optional<int32_t> Processor::TryPeekInt() const {
  if (stack_.Empty()) return std::nullopt;
  try {
    return runtime::DecodeInt32(stack_.Top(), policy_.require_minimal);
  } catch (const runtime::Exception&) {
    return std::nullopt;
  }
}

// Step execution forward by one instruction.
Processor::StepResult Processor::Step() {
  if (error_) return StepResult::Error;  // Execution already faulted, must reset.

  try {
    if (const auto instruction = parser_.Next()) Execute(*instruction);
    if (IsFinished())
      return !stack_.Empty() && stack_.TopAsBool() ? StepResult::FinishedTrue
                                                   : StepResult::FinishedFalse;
    return StepResult::Stepped;
  } catch (const runtime::Exception& e) {
    error_ = e.GetError();
    return StepResult::Error;
  }
}

void Processor::Reset(std::span<const uint8_t> script, int height) {
  parser_ = {script};
  error_.reset();
  non_push_op_count_ = 0;
  stack_.Clear();
  env_ = { height };
  machine_.emplace(stack_, script, policy_);
}

// Run the script to the end and return its Boolean result.
Processor::RunResult Processor::Run() {
  if (error_) return RunResult::Error;  // Execution already faulted, must reset.

  try {
    while (const auto instruction = parser_.Next()) Execute(*instruction);
  } catch (const runtime::Exception& e) {
    error_ = e.GetError();
    return RunResult::Error;
  }
  return !stack_.Empty() && stack_.TopAsBool() ? RunResult::True : RunResult::False;
}

void Processor::Execute(const lang::Instruction& instruction) {
  Assert(!error_);

  const bool push = lang::IsPush(instruction.opcode);
  if (!push) {
    const int max_non_push_ops = lang::MaxNonPushOps(env_.mode);
    if (non_push_op_count_++ >= max_non_push_ops)
      runtime::Throw(lang::Error::OpCountExcessive, "Hit the limit of ", max_non_push_ops,
                     "non-push operations per script.");
  }

  runtime::StepExecution(env_, *machine_, instruction);
}

}  // namespace hornet::protocol::script
