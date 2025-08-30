// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <algorithm>
#include <array>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {

void RegisterPushHandlers(Dispatcher& table);  // In stack_ops.cpp

namespace detail {
// A placeholder handler for opcodes that haven't yet been implemented.
[[noreturn]] static void OnUnknown(const Context& context) {
  util::ThrowLogicError("Opcode ", int(context.instruction.opcode), " not yet implemented.");
}

Handler GetHandler(Version version, lang::Op opcode) {
  static const auto kDispatchTable = [] {
    auto BuildDispatcher = [](Version) {
      Dispatcher handlers;
      std::fill(handlers.begin(), handlers.end(), &OnUnknown);
      RegisterPushHandlers(handlers);
      // TODO: Fill in other handler entries, depending on version.
      return handlers;
    };    
    std::array<Dispatcher, Version::Count> table;
    for (int i = 0; i < int{Version::Count}; ++i) table[i] = BuildDispatcher(Version(i));
    return table;
  }();
  return kDispatchTable[uint8_t(version)][opcode];
}

// Returns the maximum permitted number of non-push operations during a script, depending on script
// version.
inline static int MaxNonPushOps(Version version) {
  static constexpr int kMaxNonPushOps = 201;
  return version == Version::Legacy || version == Version::SegwitV0
             ? kMaxNonPushOps
             : std::numeric_limits<int>::max();
}
}  // namespace detail

void StepExecution(const Context& context) {
  // Validate the number of script operations executed.
  if (!IsPush(context.Op())) {
    const int max_non_push_ops = detail::MaxNonPushOps(context.Version());
    if (context.machine.non_push_op_count >= max_non_push_ops)
      runtime::Throw(lang::Error::OpCountExcessive, "Hit the limit of ", max_non_push_ops,
                     "non-push operations per script.");
    ++context.machine.non_push_op_count;               
  }

  // Dispatch instruction execution to the opcode handler.
  detail::GetHandler(context.Version(), context.Op())(context);
}

}  // namespace hornet::protocol::script::runtime
