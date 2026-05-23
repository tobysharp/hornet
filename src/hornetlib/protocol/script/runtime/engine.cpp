// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <algorithm>
#include <array>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {

void RegisterArithmeticHandlers(Dispatcher& table);  // In ops/arithmetic.cpp
void RegisterBitwiseHandlers(Dispatcher& table);     // In ops/bitwise.cpp
void RegisterControlHandlers(Dispatcher& table);     // In ops/control.cpp
void RegisterCryptoHandlers(Dispatcher& table);      // In ops/crypto.cpp
void RegisterSigHandlers(Dispatcher& table);         // In ops/sig.cpp
void RegisterStackHandlers(Dispatcher& table);       // In ops/stack.cpp

namespace detail {
// A placeholder handler for opcodes that haven't yet been implemented.
[[noreturn]] static void OnUnknown(const Context& context) {
  util::ThrowLogicError("Opcode ", int(context.instruction.opcode), " not yet implemented.");
}

void RegisterAllHandlers(Version, Dispatcher& handlers) {
  RegisterArithmeticHandlers(handlers);
  RegisterBitwiseHandlers(handlers);
  RegisterControlHandlers(handlers);
  RegisterCryptoHandlers(handlers);
  RegisterSigHandlers(handlers);
  RegisterStackHandlers(handlers);
  // TODO: Fill in other handler entries, depending on version.
}

Handler GetHandler(Version version, lang::Op opcode) {
  static const auto kDispatchTable = [] {
    auto BuildDispatcher = [](Version version) {
      Dispatcher handlers;
      std::fill(handlers.begin(), handlers.end(), &OnUnknown);
      RegisterAllHandlers(version, handlers);
      return handlers;
    };    
    std::array<Dispatcher, Version::Count> table;
    for (int i = 0; i < int{Version::Count}; ++i) table[i] = BuildDispatcher(Version(i));
    return table;
  }();
  return kDispatchTable[uint8_t(version)][opcode];
}

}  // namespace detail

void ExecuteHandler(lang::Op op, const Context& context) {
  // Dispatch instruction execution to the opcode handler.
  detail::GetHandler(context.Version(), op)(context);
}

void StepExecution(const Context& context) {
  // Validate the number of script operations executed.
  if (!IsPush(context.Op())) context.machine.IncNonPushOps();

  ExecuteHandler(context.Op(), context);
}

}  // namespace hornet::protocol::script::runtime
