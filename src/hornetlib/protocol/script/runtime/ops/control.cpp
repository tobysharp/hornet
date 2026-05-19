// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

using namespace lang;

// Op::Verify
static void OnVerify(const Context& context) {
  context.Call([&](Bytes input) { if (!AsBool(input)) Throw(Error::OpVerify); });
}

// Op::Nop
static void OnNop(const Context&) {
}

// Register handlers
void RegisterControlHandlers(Dispatcher& table) {
  table[Op::Nop] = &OnNop;
  table[Op::Verify] = &OnVerify;
}

}  // namespace hornet::protocol::script::runtime
