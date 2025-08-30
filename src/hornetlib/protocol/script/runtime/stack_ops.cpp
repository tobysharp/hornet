#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/types.h"
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
static void OnPushEmpty(const Context& context) {
  if (context.RequiresMinimal()) detail::VerifyMinimal(context.instruction);
  context.machine.stack.Push(lang::Bytes{});
}

// Op::PushSize1 ... Op::PushData4
static void OnPushData(const Context& context) {
  if (context.RequiresMinimal()) detail::VerifyMinimal(context.instruction);
  context.machine.stack.Push(context.instruction.data);
}

// Op::PushConstNegative1 ... Op::PushConst16
template <int8_t N>
static void OnPushConst(const Context& context) {
  context.machine.stack.Push(lang::EncodeMinimalConst<N>());
}

// Op::Duplicate
static void OnDuplicate(const Context& context) {
  context.machine.stack.Push(context.machine.stack.Top());
}

// Op::Drop
static void OnDrop(const Context& context) {
  context.machine.stack.Pop();
}

void RegisterPushHandlers(Dispatcher& table) {
  table[lang::Op::PushEmpty] = &OnPushEmpty;
  for (auto op = lang::Op::PushSize1; op <= lang::Op::PushData4; ++op)
    table[op] = &OnPushData;
  table[lang::Op::PushConstNegative1] = &OnPushConst<-1>;
  util::UnrollRange<1, 16 + 1>(
      [&](auto i) { table[lang::ImmediateToOp(i)] = &OnPushConst<i>; });
  table[lang::Op::Duplicate] = &OnDuplicate;
  table[lang::Op::Drop] = &OnDrop;
}

}  // namespace hornet::protocol::script::runtime
