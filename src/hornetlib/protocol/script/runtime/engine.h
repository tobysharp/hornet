// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>

#include "hornetlib/crypto/signature.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/stack.h"
#include "hornetlib/protocol/script/satisfy.h"
#include "hornetlib/protocol/script/spend.h"
#include "hornetlib/protocol/transaction.h"

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
  FeatureFlags features = {};
};

// The virtual machine state.
struct Machine {
  // Mutable machine state.
  runtime::Stack& stack;
  runtime::Stack& altstack;
  runtime::ConditionStack& conditions;
  int non_push_op_count = 0;
  const int max_non_push_ops = 201;

  // Immutable machine state.
  const lang::Bytes script;
  int code_separator_offset = 0;

  // Immutable execution policy.
  const Policy& policy;

  int32_t DecodeInt32(lang::Bytes bytes) const { return runtime::DecodeInt32(bytes, policy.require_minimal); }
  lang::Bytes ScriptCode() const { return script.subspan(code_separator_offset); }
  void SetCodeSeparator(const lang::Instruction& instruction) {
    code_separator_offset = instruction.raw.data() + instruction.raw.size() - script.data();
  }
  void IncNonPushOps(int count = 1) {
    if (non_push_op_count + count > max_non_push_ops)
      runtime::Throw(lang::Error::OpCountExcessive, "Hit the limit of ", max_non_push_ops,
                     "non-push operations per script.");
    non_push_op_count += count;
  }
};

// The external environment in which the script execution is contextualized:
// the transaction, block height, address type, etc.
struct Environment {
  // int height = 0;
  Version version = Version::Legacy;
  std::optional<SpendContext> spend;
};

// All of the above script execution context, grouped for convenience.
struct Context {
  const Environment& env;                // The external environment of execution.
  Machine& machine;                      // The virtual machine state.
  const lang::Instruction& instruction;  // The current instruction.

  bool RequiresMinimal() const { return machine.policy.require_minimal; }
  bool IsStrictDER() const { return machine.policy.features.Has(Feature::StrictDER); }
  bool IsNullDummy() const { return machine.policy.features.Has(Feature::NullDummy); }
  bool IsCheckLockTimeVerify() const { return machine.policy.features.Has(Feature::CheckLockTimeVerify); }
  bool IsCheckSequenceVerify() const { return machine.policy.features.Has(Feature::CheckSequenceVerify); }

  Stack& AltStack() const { return machine.altstack; }
  Stack& Stack() const { return machine.stack; }
  ConditionStack& Conditions() const { return machine.conditions; }
  Version Version() const { return env.version; }
  lang::Op Op() const { return instruction.opcode; }
  const SpendContext& Spend() const {
    if (!env.spend) Throw(lang::Error::InvalidSpendContext);
    return *env.spend;
  }
  crypto::ecdsa::DERParseType DERParsing() const {
    return IsStrictDER() ? crypto::ecdsa::DERParseType::Strict : crypto::ecdsa::DERParseType::Lax;
  }
  template <typename Fn>
  void Call(Fn&& fn) const {
    machine.stack.Call(std::forward<Fn>(fn));
  }
  lang::Bytes ScriptCode() const { return machine.ScriptCode(); }
  int32_t Int32(int pos = 0) const { return Stack().Int32(pos, RequiresMinimal()); }
  template <std::integral T, int kBytes>
  T DecodeTop() const { return Decode<T, kBytes>(Stack().Top(), RequiresMinimal()); }
};

using Handler = void (*)(const Context&);

struct Dispatcher : public std::array<Handler, 256> {
  using Base = std::array<Handler, 256>;
  Handler& operator[](lang::Op op) { return Base::operator[](uint8_t(op)); }
  const Handler& operator[](lang::Op op) const { return Base::operator[](uint8_t(op)); }
  std::array<Handler, 256> entries;
};

void StepExecution(const Context&);
void ExecuteHandler(lang::Op op, const Context& context);

}  // namespace hornet::protocol::script::runtime
