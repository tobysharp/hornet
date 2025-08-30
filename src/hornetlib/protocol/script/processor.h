#pragma once

#include <optional>
#include <span>

#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol::script {

class Processor {
 public:
  enum class RunResult { True, False, Error };
  enum class StepResult { Stepped, FinishedTrue, FinishedFalse, Error };

  explicit Processor(std::span<const uint8_t> script,
    bool require_minimal = true,  // Becomes execution policy
    int height = 0                // Becomes environment context
  ) : parser_(script), policy_{require_minimal}, env_{height, lang::Mode::Legacy},
  machine_(runtime::Machine{stack_, parser_.Script(), policy_}) {}

  RunResult Run();
  StepResult Step();
  std::optional<lang::Error> LastError() const { return error_; }
  bool IsFinished() const { return !parser_.Peek(); }
  void Reset(std::span<const uint8_t> script, int height);

  // Try to interpret the top-of-stack as a 32-bit signed integer, if valid.
  std::optional<int32_t> TryPeekInt() const;

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
  int non_push_op_count_ = 0;
};

}  // namespace hornet::protocol::script
