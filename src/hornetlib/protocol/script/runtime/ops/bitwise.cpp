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

using lang::Bytes;
using lang::Op;

// Op::Equal
static void OnEqual(const Context& context) {
  return context.Stack().Call([](const Bytes a, const Bytes b) {
    return std::ranges::equal(a, b);
  });
}

// Register handlers
void RegisterBitwiseHandlers(Dispatcher& table) {
  table[Op::Equal] = &OnEqual;
}

}  // namespace hornet::protocol::script::runtime

