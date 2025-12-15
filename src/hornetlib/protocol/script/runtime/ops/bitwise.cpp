// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <algorithm>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"
#include "hornetlib/util/as_span.h"

namespace hornet::protocol::script::runtime {

using lang::Op;

template <typename Fn>
inline void BinaryBitwise(const Context& context, Fn&& f) {
  auto& stack = context.Stack();

  // Retrieve the stack items as references to the internal data.
  const auto& x1 = stack.At(1);
  const auto& x2 = stack.At(0);

  // Compute the binary function.
  const bool out = f(x1, x2);

  // Pop the inputs and push the output.
  stack.Pop(2).Push(out);
}

// Op::Equal
static void OnEqual(const Context& context) {
  return BinaryBitwise(context, [](const auto& a, const auto& b) {
    return std::ranges::equal(a, b);
  });
}

// Register handlers
void RegisterBitwiseHandlers(Dispatcher& table) {
  table[Op::Equal] = &OnEqual;
}

}  // namespace hornet::protocol::script::runtime

