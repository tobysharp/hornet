#include <algorithm>
#include <array>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/dispatch.h"

namespace hornet::protocol::script::runtime {

void RegisterPushHandlers(Dispatcher& table);  // In push.cpp

namespace detail {
static Dispatcher BuildDispatcher(lang::Mode /*mode*/) {
  Dispatcher handlers;
  std::fill(handlers.begin(), handlers.end(), &OnUnknown);
  RegisterPushHandlers(handlers);
  // TODO: Fill in other handler entries, depending on mode.
  return handlers;
}
}  // namespace detail

const auto kDispatchTable = []{
    std::array<Dispatcher, lang::Mode::Count> table;
    for (int i = 0; i < int{lang::Mode::Count}; ++i)
        table[i] = detail::BuildDispatcher(lang::Mode(i));
    return table;
}();

void StepExecution(const Environment& env, Machine& machine,
                          const lang::Instruction& instruction) {
  const Handler handler = kDispatchTable[uint8_t(env.mode)][uint8_t(instruction.opcode)];
  handler(env, machine, instruction);
}

}  // namespace hornet::protocol::script::runtime
