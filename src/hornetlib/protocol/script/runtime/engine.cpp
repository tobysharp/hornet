#include <algorithm>
#include <array>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"

namespace hornet::protocol::script::runtime {

void RegisterPushHandlers(Dispatcher& table);  // In stack_ops.cpp

namespace detail {
[[noreturn]] static void OnUnknown(const Context& context) {
  util::ThrowLogicError("Opcode ", int(context.instruction.opcode), " not yet implemented.");
}

static Dispatcher BuildDispatcher(lang::Mode /*mode*/) {
  Dispatcher handlers;
  std::fill(handlers.begin(), handlers.end(), &OnUnknown);
  RegisterPushHandlers(handlers);
  // TODO: Fill in other handler entries, depending on mode.
  return handlers;
}
}  // namespace detail

const auto kDispatchTable = [] {
  std::array<Dispatcher, lang::Mode::Count> table;
  for (int i = 0; i < int{lang::Mode::Count}; ++i)
    table[i] = detail::BuildDispatcher(lang::Mode(i));
  return table;
}();

void StepExecution(const Context& context) {
  const Handler handler =
      kDispatchTable[uint8_t(context.env.mode)][context.instruction.opcode];
  handler(context);
}

}  // namespace hornet::protocol::script::runtime
