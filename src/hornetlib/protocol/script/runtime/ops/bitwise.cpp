// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <algorithm>

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

using namespace lang;

// Op::Equal
static void OnEqual(const Context& context) {
  context.Call([](Bytes a, Bytes b) { return std::ranges::equal(a, b); });
}

// Op::EqualVerify
static void OnEqualVerify(const Context& context) {
  context.Call([](Bytes a, Bytes b) { return std::ranges::equal(a, b); });
  context.Verify(Error::OpEqualVerify);
}

// Register handlers
void RegisterBitwiseHandlers(Dispatcher& table) {
  table[Op::Equal] = &OnEqual;
  table[Op::EqualVerify] = &OnEqualVerify;
}

}  // namespace hornet::protocol::script::runtime
