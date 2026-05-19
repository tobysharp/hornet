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

// Op::Hash160
static void OnHash160(const Context& context) {
  context.Call([&](Bytes arg) { return crypto::ComputeHash160(arg.begin(), arg.end()); });
}

// Register handlers
void RegisterCryptoHandlers(Dispatcher& table) {
  table[Op::Hash160] = &OnHash160;
}

}  // namespace hornet::protocol::script::runtime
