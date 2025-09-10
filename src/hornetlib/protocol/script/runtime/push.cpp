#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/dispatch.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/unroll.h"

namespace hornet::protocol::script::runtime {

namespace detail {
inline static void VerifyMinimal(const lang::Instruction& instruction) {
  const lang::Op minimal_op = [](lang::Bytes data) {
    if (data.empty())
      return lang::Op::PushEmpty;
    else if (data.size() == 1 && data[0] >= 1 && data[0] <= 16)
      return lang::ImmediateToOp(data[0]);
    else if (data.size() == 1 && data[0] == 0x81)
      return lang::Op::PushConstNegative1;
    else if (data.size() <= uint8_t(lang::Op::PushSizeMax))
      return lang::Op::PushSize1 + (std::ssize(data) - 1);
    else if (data.size() <= 0xFF)
      return lang::Op::PushData1;
    else if (data.size() <= 0xFFFF)
      return lang::Op::PushData2;
    else
      return lang::Op::PushData4;
  }(instruction.data);
  if (instruction.opcode != minimal_op)
    Throw(lang::Error::NonMinimalPushOp, "Opcode ", int(instruction.opcode),
          " was not the minimal encoding.");
}
}  // namespace detail

// Op::PushEmpty
static void OnPushEmpty(const Environment&, Machine& machine,
                        const lang::Instruction& instruction) {
  if (machine.policy.require_minimal) detail::VerifyMinimal(instruction);
  machine.stack.Push(lang::Bytes{});
}

// Op::PushSize1 ... Op::PushData4
static void OnPushData(const Environment&, Machine& machine, const lang::Instruction& instruction) {
  if (machine.policy.require_minimal) detail::VerifyMinimal(instruction);
  machine.stack.Push(instruction.data);
}

// Op::PushConstNegative1 ... Op::PushConst16
template <int8_t N>
static void OnPushConst(const Environment&, Machine& machine, const lang::Instruction&) {
  machine.stack.Push(lang::EncodeMinimalConst<N>());
}

void RegisterPushHandlers(Dispatcher& table) {
  table[uint8_t(lang::Op::PushEmpty)] = &OnPushEmpty;
  for (uint8_t op = uint8_t(lang::Op::PushSize1); op <= uint8_t(lang::Op::PushData4); ++op)
    table[op] = &OnPushData;
  table[uint8_t(lang::Op::PushConstNegative1)] = &OnPushConst<-1>;
  util::UnrollRange<1, 16 + 1>(
      [&](auto i) { table[uint8_t(lang::ImmediateToOp(i))] = &OnPushConst<i>; });
}

}  // namespace hornet::protocol::script::runtime
