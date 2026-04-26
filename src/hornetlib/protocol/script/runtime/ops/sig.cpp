// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <optional>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/signature.h"
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

// Op::CheckSig
static void OnCheckSig(const Context& context) {
  // sig pubkey -- bool
  context.Stack().Call([&](Bytes sigblob, Bytes pkblob) -> bool {
    using namespace crypto::ecdsa;
    Assert(context.env.spend.has_value());

    if (sigblob.empty()) return false;

    // TODO: Tapscript path: EvalChecksigTapscript.
    // TODO: Check signature and pubkey encodings: CheckSignatureEncoding, CheckPubKeyEncoding.

    // Create the spend digest, which is a 32-byte hash of transaction bytes committing to the spend.
    const auto digest = BuildSpendDigest(*context.env.spend, sigblob, context.machine.script);

    // Extract the DER-encoded ECDSA signature, which is up to 72 bytes.
    const DERParseType parse_method = context.machine.policy.require_strict_der_signatures ? DERParseType::Strict : DERParseType::Lax;
    const auto signature = ParseSignatureDER<secp256k1::Wide>(sigblob.first(sigblob.size() - 1), parse_method);
    if (!signature) return false;
  
    // The public key is in 65-byte uncompressed SEC1 format.
    const auto pubkey = secp256k1::PublicKeyFromUncompressed(pkblob);
    if (!pubkey) return false;

    // Verify that the spend digest was signed by the private key corresponding to this public key.
    return secp256k1::VerifySignature(*pubkey, *signature, digest);
  });
}

void RegisterSigHandlers(Dispatcher& table) {
  table[Op::CheckSig] = &OnCheckSig;
}

}  // namespace hornet::protocol::script::runtime
