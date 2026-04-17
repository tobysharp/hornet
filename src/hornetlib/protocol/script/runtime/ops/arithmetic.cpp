// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

using lang::Bytes;
using lang::Op;

template <typename Fn>
inline void BinaryInt32(const Context& context, Fn&& f) {
  context.Stack().Call([&](Bytes lhs, Bytes rhs) -> int64_t {
    const int64_t a = context.machine.DecodeInt32(lhs);
    const int64_t b = context.machine.DecodeInt32(rhs);
    return f(a, b);
  });
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
