// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include "hornetlib/crypto/hash.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

using namespace lang;

// Op::CodeSeparator = 0xab
static void OnCodeSeparator(const Context& context) {
  context.machine.SetCodeSeparator(context.instruction);
}

// Op::Hash160 = 0xa9
static void OnHash160(const Context& context) {
  context.Call([&](Bytes arg) { return crypto::ComputeHash160(arg); });
}

// Op::SHA256 = 0xa8
static void OnSHA256(const Context& context) {
  context.Call([&](Bytes arg) { return crypto::Sha256(arg); });
}

// Register handlers
void RegisterCryptoHandlers(Dispatcher& table) {
  table[Op::CodeSeparator] = &OnCodeSeparator;
  table[Op::Hash160] = &OnHash160;
  table[Op::SHA256] = &OnSHA256;
}

}  // namespace hornet::protocol::script::runtime
