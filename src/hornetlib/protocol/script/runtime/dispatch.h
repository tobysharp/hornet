#pragma once

#include <array>
#include <cstdint>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/util/throw.h"

namespace hornet::protocol::script::runtime {

using Handler = void (*)(const Environment& env, Machine& machine,
                         const lang::Instruction& instruction);

[[noreturn]] inline void OnUnknown(const Environment&, Machine&, const lang::Instruction& instruction) {
  util::ThrowLogicError("Opcode ", int(instruction.opcode), " not yet implemented.");
}

using Dispatcher = std::array<Handler, 256>;

}  // namespace hornet::protocol::script::runtime
