// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/stack.h"

namespace hornet::protocol::script::runtime {

// The Bitcoin Script execution version used by the interpreter, depending on the
// output address type (and spending path for Taproot). The versions vary in some of
// the opcodes and enforced limits.
enum Version {
  Legacy,     // Bare scripts (pre-BIP16), P2PKH, or non-witness P2SH redeemScript.
  SegwitV0,   // Witness v0 (P2WPKH or P2WSH), native or P2SH-nested. Since BIP141.
  Tapscript,  // Witness v1 (Taproot script-path). Since BIP342.
  Count       // Number of execution versions in this enum.
};

// Execution policy defines specific rules for the script interpreter to follow.
struct Policy {
  bool require_minimal = true;
};

// The virtual machine state.
struct Machine {
  // Mutable machine state.
  runtime::Stack& stack;
  int non_push_op_count = 0;

  // Immutable machine state.
  const lang::Bytes script;

  // Immutable execution policy.
  const Policy& policy;
};

// The external environment in which the script execution is contexualized:
// the transaction, block height, address type, etc.
struct Environment {
  int height = 0;
  Version version = Version::Legacy;
};

// All of the above script execution context, grouped for convenience.
struct Context {
  const Environment& env;                // The external environment of execution.
  Machine& machine;                      // The virtual machine state.
  const lang::Instruction& instruction;  // The current instruction.

  bool RequiresMinimal() const { return machine.policy.require_minimal; }
  Stack& Stack() const { return machine.stack; }
  Version Version() const { return env.version; }
  lang::Op Op() const { return instruction.opcode; }
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
