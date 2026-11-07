// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include "hornetlib/crypto/hash.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/throw.h"

namespace hornet::protocol::script::runtime {

namespace {

using lang::Bytes;

// Op::CodeSeparator = 0xab
void OnCodeSeparator(const Context& context) {
  context.machine.SetCodeSeparator(context.instruction);
}

template <auto Fn>
void OnHash(const Context& context) {
  context.Call([&](Bytes arg) { return Fn(arg); });
}

}  // namespace

// Register handlers
void RegisterCryptoHandlers(Dispatcher& table) {
  using lang::Op;
  table[Op::CodeSeparator] = &OnCodeSeparator;
  table[Op::Hash160]   = &OnHash<crypto::Hash160>;
  table[Op::Hash256]   = &OnHash<crypto::Hash256>;
  table[Op::RIPEMD160] = &OnHash<crypto::Ripemd160>;
  table[Op::SHA1]      = &OnHash<crypto::Sha1>;
  table[Op::SHA256]    = &OnHash<crypto::Sha256>;
}

}  // namespace hornet::protocol::script::runtime
