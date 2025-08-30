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

struct Context {
  const Environment& env;
  Machine& machine;
  const lang::Instruction& instruction;

  bool RequiresMinimal() const { return machine.policy.require_minimal; }
};

using Handler = void (*)(const Context&);

struct Dispatcher : public std::array<Handler, 256> {
  using Base = std::array<Handler, 256>;
  Handler& operator[](lang::Op op) {
    return Base::operator [](uint8_t(op));
  }
  const Handler& operator[](lang::Op op) const {
    return Base::operator [](uint8_t(op));
  }
  std::array<Handler, 256> entries;
};

void StepExecution(const Context&);

}  // namespace hornet::protocol::script::runtime
