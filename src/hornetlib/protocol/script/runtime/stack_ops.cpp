// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/lang/minimal.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/unroll.h"

namespace hornet::protocol::script::runtime {

using lang::Op;

namespace detail {
inline static void VerifyMinimal(const lang::Instruction& instruction) {
  const Op minimal_op = [](lang::Bytes data) {
    if (lang::IsEncodedZero(data))
      return Op::PushEmpty;
    else if (data.size() == 1 && data[0] >= 1 && data[0] <= 16)
      return lang::ImmediateToOp(data[0]);
    else if (data.size() == 1 && data[0] == 0x81)
      return Op::PushConstNegative1;
    else if (data.size() <= uint8_t(Op::PushSizeMax))
      return Op::PushSize1 + (std::ssize(data) - 1);
    else if (data.size() <= 0xFF)
      return Op::PushData1;
    else if (data.size() <= 0xFFFF)
      return Op::PushData2;
    else
      return Op::PushData4;
  }(instruction.data);
  if (instruction.opcode != minimal_op)
    Throw(lang::Error::NonMinimalPush, "Opcode ", int(instruction.opcode),
          " was not the minimal encoding.");
}
}  // namespace detail

// Op::PushEmpty
static void OnPushEmpty(const Context& context) {
  if (context.RequiresMinimal()) detail::VerifyMinimal(context.instruction);
  context.Stack().Push(lang::Bytes{});
}

// Op::PushSize1 ... Op::PushData4
static void OnPushData(const Context& context) {
  if (context.RequiresMinimal()) detail::VerifyMinimal(context.instruction);
  context.Stack().Push(context.instruction.data);
}

// Op::PushConstNegative1 ... Op::PushConst16
template <int8_t N>
static void OnPushConst(const Context& context) {
  context.Stack().Push(lang::EncodeMinimalConst<N>());
}

// Op::Duplicate
static void OnDuplicate(const Context& context) {
  context.Stack().Push(context.Stack().Top());
}

// Op::Drop
static void OnDrop(const Context& context) {
  context.Stack().Pop();
}

void RegisterPushHandlers(Dispatcher& table) {
  table[Op::PushEmpty] = &OnPushEmpty;
  for (auto op = Op::PushSize1; op <= Op::PushData4; ++op) table[op] = &OnPushData;
  table[Op::PushConstNegative1] = &OnPushConst<-1>;
  util::UnrollRange<1, 16 + 1>([&](auto i) { table[lang::ImmediateToOp(i)] = &OnPushConst<i>; });
  table[Op::Duplicate] = &OnDuplicate;
  table[Op::Drop] = &OnDrop;
}

}  // namespace hornet::protocol::script::runtime
