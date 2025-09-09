// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

using lang::Op;

template <typename Fn>
inline void BinaryInt32(const Context& context, Fn&& f) {
  auto& stack = context.Stack();

  // Decode the stack items into integer format. 
  const int64_t x1 = stack.Int32(1, context.RequiresMinimal());
  const int64_t x2 = stack.Int32(0, context.RequiresMinimal());

  // Compute the binary function.
  const int64_t out = f(x1, x2);

  // Pop the inputs and push the output.
  stack.Pop(2).PushInt(out);
}

// Op::Add
static void OnAdd(const Context& context) {
  BinaryInt32(context, [](int64_t a, int64_t b) { return a + b; });
}

// Register handlers
void RegisterArithmeticHandlers(Dispatcher& table) {
  table[Op::Add] = &OnAdd;
}

}  // namespace hornet::protocol::script::runtime
