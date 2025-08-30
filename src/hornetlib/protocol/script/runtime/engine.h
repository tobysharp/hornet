#pragma once

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/stack.h"

namespace hornet::protocol::script::runtime {

struct Policy {
  bool require_minimal = true;
};

struct Machine {
  // Mutable machine state.
  runtime::Stack& stack;

  // Immutable machine state.
  const lang::Bytes script;

  // Immutable execution policy.
  const Policy& policy;
};

struct Environment {
  int height = 0;
  lang::Mode mode = lang::Mode::Legacy;
};

void StepExecution(const Environment& env, Machine& machine,
                          const lang::Instruction& instruction);

}  // namespace hornet::protocol::script::runtime
