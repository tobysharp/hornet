// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/signing.h"
#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/util/big_uint.h"

namespace hornet::protocol::script::runtime {

using lang::Bytes;
using lang::Op;

namespace {

// TODO
bool VerifyECDSASignature(const Hash& hash, Bytes signature, Bytes pubkey) {
  // TODO: Check valid public key.

  (void)hash;
  (void)signature;
  (void)pubkey;
  return true;
}

}  // namespace

// Op::CheckSig
static void OnCheckSig(const Context& context) {
  // sig pubkey -- bool
  context.Stack().Call([&](Bytes sig, Bytes pubkey) -> bool {
    if (sig.empty()) return false;
    Assert(context.env.spend.has_value());

    // TODO: Tapscript path: EvalChecksigTapscript.
    // TODO: Check signature and pubkey encodings: CheckSignatureEncoding, CheckPubKeyEncoding.

    // Create the spend digest, which is a 32-byte hash of transaction bytes committing to the spend.
    const auto digest = BuildSpendDigest(*context.env.spend, sig, context.machine.script);

    // Extract the DER-encoded ECDSA signature, which is up to 72 bytes.
    const auto signature = sig.first(sig.size() - 1);
    Assert(signature.size() <= 72u && signature[0] == 0x30);
  
    // The public key is in 65-byte uncompressed SEC1 format.
    Assert(pubkey.size() == 65u && pubkey[0] == 0x04);

    // Verify that the spend digest was signed by the private key corresponding to this public key.
    return VerifyECDSASignature(digest, signature, pubkey);
  });
}

void RegisterSigHandlers(Dispatcher& table) {
  table[Op::CheckSig] = &OnCheckSig;
}

}  // namespace hornet::protocol::script::runtime
