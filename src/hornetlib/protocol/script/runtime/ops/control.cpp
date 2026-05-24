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



template <bool kNegate>
static void OnIf(const Context& context) {
  // <expression> if [statements] [else [statements]] endif
  auto& conditions = context.Conditions();
  bool value = false;
  if (conditions) {
    const bool tos = context.Stack().TopAsBool();
    value = kNegate ? !tos : tos;
    context.Stack().Pop();
  }
  conditions.Push(value);
}

// Op::Else
static void OnElse(const Context& context) {
  context.Conditions().Toggle();
}

// Op::EndIf
static void OnEndIf(const Context& context) {
  context.Conditions().Pop();
}

// Op::Verify
static void OnVerify(const Context& context) {
  context.Call([&](Bytes input) {
    if (!AsBool(input)) Throw(Error::OpVerify);
  });
}

// Op::Nop
static void OnNop(const Context&) {}

// Register handlers
void RegisterControlHandlers(Dispatcher& table) {
  table[Op::Else] = &OnElse;
  table[Op::EndIf] = &OnEndIf;
  table[Op::If] = &OnIf<false>;
  table[Op::Nop] = table[Op::Nop1] = table[Op::Nop4] = table[Op::Nop5] = table[Op::Nop6] = table[Op::Nop7] =
      table[Op::Nop8] = table[Op::Nop9] = table[Op::Nop10] = &OnNop;
  table[Op::NotIf] = &OnIf<true>;
  table[Op::Verify] = &OnVerify;
}

}  // namespace hornet::protocol::script::runtime
