// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

namespace {

using namespace lang;

template <bool kNegate>
void OnIf(const Context& context) {
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
void OnElse(const Context& context) {
  context.Conditions().Toggle();
}

// Op::EndIf
void OnEndIf(const Context& context) {
  context.Conditions().Pop();
}

// Op::Nop
void OnNop(const Context&) {}

// Op::Return
void OnReturn(const Context&) {
  Throw(Error::OpReturn);
}

// Op::Verify
void OnVerify(const Context& context) {
  context.Verify(Error::OpVerify);
}

}  // namespace

// Register handlers
void RegisterControlHandlers(Dispatcher& table) {
  table[Op::Else] = &OnElse;
  table[Op::EndIf] = &OnEndIf;
  table[Op::If] = &OnIf<false>;
  table[Op::Nop] = table[Op::Nop1] = table[Op::Nop4] = table[Op::Nop5] = table[Op::Nop6] = table[Op::Nop7] =
      table[Op::Nop8] = table[Op::Nop9] = table[Op::Nop10] = &OnNop;
  table[Op::NotIf] = &OnIf<true>;
  table[Op::Return] = &OnReturn;
  table[Op::Verify] = &OnVerify;
}

}  // namespace hornet::protocol::script::runtime
