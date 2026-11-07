// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/decode.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/unroll.h"

namespace hornet::protocol::script::runtime {

namespace {
using lang::Bytes;
using lang::Op;

inline static void VerifyMinimal(const lang::Instruction& instruction) {
  const Op minimal_op = [](lang::Bytes data) {
    if (lang::IsEncodedZero(data))
      return Op::PushEmpty;
    else if (data.size() == 1 && data[0] >= 1 && data[0] <= 16)
      return lang::ConstantToOp(data[0]);
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

template <auto kIndices>
consteval int MaxElement() {
  return *std::max_element(std::begin(kIndices), std::end(kIndices));
}

template <auto kIndices, int kPopCount = MaxElement<kIndices>() + 1>
void OnModify(const Context& context) {
  context.Stack().ModifyTop<kPopCount, kIndices>();
}

template <auto kIndices>
void OnCopy(const Context& context) {
  context.Stack().CopyToTop<kIndices>();
}

// Modify schedules
constexpr std::array<int, 0> kDrop = {};
constexpr std::array kRotate = { 2, 0, 1 };
constexpr std::array kRotate2 = { 4, 5, 0, 1, 2, 3 };
constexpr std::array kSwap = { 1, 0 };
constexpr std::array kSwap2 = { 2, 3, 0, 1 };
constexpr std::array kTuck = { 0, 1, 0 };

// Copy schedules
constexpr std::array kDuplicate = { 0 };
constexpr std::array kDuplicate2 = { 0, 1 };
constexpr std::array kDuplicate3 = { 0, 1, 2 };
constexpr std::array kOver = { 1 };
constexpr std::array kOver2 = { 2, 3 };

// Op::Depth
void OnDepth(const Context& context) {
  context.Stack().Push(context.Stack().Size());
}

// Op::FromAltStack
void OnFromAltStack(const Context& context) {
  context.AltStack().Call([&](Bytes arg) { context.Stack().Push(arg); });
}

// Op::IfDup
void OnIfDup(const Context& context) {
  if (context.Stack().TopAsBool()) context.Stack().CopyToTop(0);
}

// Op::Pick
void OnPick(const Context& context) {
  const int position = context.Int32();
  context.Stack().Pop().CopyToTop(position);
}

// Op::Roll
void OnRoll(const Context& context) {
  const int position = context.Int32();
  context.Stack().Pop().MoveToTop(position);
}

// Op::PushConstNegative1 ... Op::PushConst16
template <int8_t N>
void OnPushConst(const Context& context) {
  const uint8_t byte = lang::EncodeMinimalConst<N>();
  context.Stack().Push({&byte, 1});
}

// Op::PushEmpty
void OnPushEmpty(const Context& context) {
  if (context.RequiresMinimal()) VerifyMinimal(context.instruction);
  context.Stack().Push(Bytes{});
}

// Op::PushSize1 ... Op::PushData4
void OnPushData(const Context& context) {
  if (context.RequiresMinimal()) VerifyMinimal(context.instruction);
  context.Stack().Push(context.instruction.data);
}

// Op::Size
void OnSize(const Context& context) {
  context.Stack().Push(std::ssize(context.Stack().Top()));
}

// Op::ToAltStack
void OnToAltStack(const Context& context) {
  context.Call([&](Bytes arg) { context.AltStack().Push(arg); });
}

}  // namespace

void RegisterStackHandlers(Dispatcher& table) {
  table[Op::Depth] = &OnDepth;
  table[Op::Drop] = &OnModify<kDrop, 1>;
  table[Op::Drop2] = &OnModify<kDrop, 2>;
  table[Op::Duplicate] = &OnCopy<kDuplicate>;
  table[Op::Duplicate2] = &OnCopy<kDuplicate2>;
  table[Op::Duplicate3] = &OnCopy<kDuplicate3>;
  table[Op::FromAltStack] = &OnFromAltStack;
  table[Op::IfDup] = &OnIfDup;
  table[Op::Nip] = &OnModify<kDuplicate, 2>;
  table[Op::Over] = &OnCopy<kOver>;
  table[Op::Over2] = &OnCopy<kOver2>;
  table[Op::Pick] = &OnPick;
  util::UnrollRange<1, 16 + 1>([&](auto i) { table[lang::ConstantToOp(i)] = &OnPushConst<i>; });
  table[Op::PushConstNegative1] = &OnPushConst<-1>;
  for (auto op = Op::PushSize1; op <= Op::PushData4; ++op) table[op] = &OnPushData;
  table[Op::PushEmpty] = &OnPushEmpty;
  table[Op::Roll] = &OnRoll;
  table[Op::Rotate] = &OnModify<kRotate>;
  table[Op::Rotate2] = &OnModify<kRotate2>;
  table[Op::Size] = &OnSize;
  table[Op::Swap] = &OnModify<kSwap>;
  table[Op::Swap2] = &OnModify<kSwap2>;
  table[Op::Tuck] = &OnModify<kTuck>;
  table[Op::ToAltStack] = &OnToAltStack;
}

}  // namespace hornet::protocol::script::runtime

  // Depth = 0x74,
