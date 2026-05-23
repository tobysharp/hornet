// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <optional>
#include <ranges>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/signature.h"
#include "hornetlib/encoding/writer.h"
#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/runtime/signing.h"
#include "hornetlib/protocol/script/writer.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/big_uint.h"
#include "hornetlib/util/subarray.h"

namespace hornet::protocol::script::runtime {

using lang::Bytes;
using lang::Error;
using lang::Op;

static std::vector<uint8_t> PrepareScriptCode(Bytes script_code, std::span<const Bytes> sigblobs) {
  std::vector<uint8_t> buffer;
  std::vector<util::SubArray<uint8_t>> matches;
  for (Bytes sigblob : sigblobs) {
    const auto sig_push = Writer{}.PushData(sigblob).Release();
    matches.emplace_back(buffer.size(), sig_push.size());
    buffer.append_range(sig_push);
  }

  std::vector<uint8_t> result;
  Parser parser{script_code};
  while (auto instruction = parser.Next()) {
    bool found = false;
    for (auto match : matches) {
      if ((found |= std::ranges::equal(instruction->raw, match.Span(buffer)))) break;
    }
    if (!found) result.append_range(instruction->raw);
  }
  result.append_range(parser.Tail());
  return result;
}

// Op::CheckMultiSig
static void OnCheckMultiSig(const Context& context) {
  // ([sig ...] num_of_signatures [pubkey ...] num_of_pubkeys -- bool)
  context.Call([&] -> bool {
    using namespace crypto::ecdsa;
    constexpr int kMaxPubKeysPerMultiSig = 20;

    // Decode the number of public keys, and add it to the count of non-push opcodes executed.
    const int key_count = context.Int32(0);
    if (key_count < 0 || key_count > kMaxPubKeysPerMultiSig) Throw(Error::MultiSigKeyCount);
    context.machine.IncNonPushOps(key_count);

    // Decode the number of signatures.
    const int sig_count = context.Int32(key_count + 1);
    if (sig_count < 0 || sig_count > key_count) Throw(Error::MultiSigSigCount);

    // Prepare the script code. For legacy, this includes stripping the sig bytes out of the script.
    std::array<Bytes, kMaxPubKeysPerMultiSig> sigblobs = {};
    for (int i = 0; i < sig_count; ++i) sigblobs[i] = context.Stack().Peek(1 + key_count + 1 + i);
    const auto script_code = PrepareScriptCode(context.ScriptCode(), std::span{sigblobs}.first(sig_count));

    // Iterate over signatures present, requiring each to be verified against a public key.
    int sig_pos = 0, key_pos = 0;
    while (sig_pos < sig_count) {
      Hash digest;
      std::optional<secp256k1::Signature> signature;
      if (const auto sigblob = sigblobs[sig_pos]; !sigblob.empty()) {
        // Remove the sighash type byte to get the DER signature bytes.
        const auto der_bytes = sigblob.first(sigblob.size() - 1);
        // Parse the DER signature, respecting the strictness feature.
        if (signature = secp256k1::ParseDERSignature(der_bytes, context.DERParsing()); !signature && context.IsStrictDER())
          Throw(Error::InvalidDERSignature);
        // Build the spend digest (aka sighash) that the signature commits to.
        if (signature) digest = BuildSpendDigest(*context.env.spend, sigblob, script_code);
      }

      // Search for a pubkey for which the current signature is verified.
      bool matched = false;
      while (key_pos < key_count && !matched) {
        // Parse the public key from SEC1 format.
        const auto pubkey = secp256k1::PublicKeyFromSEC1(context.Stack().Peek(1 + key_pos++));
        // If both pubkey and signature are valid, perform the ECDSA verify operation.
        if (pubkey && signature && (matched = secp256k1::VerifySignature(*pubkey, *signature, digest))) ++sig_pos;
        // If there are not enough keys remaining to verify all signatures, we can fail early.
        else if (key_count - key_pos < sig_count - sig_pos) break;
      }
      if (!matched) break;
    }

    // Pop the args from the stack
    context.Stack().Pop(key_count + sig_count + 2);

    // Due to historical artifacts, there must be an additional empty stack item.
    if (context.IsNullDummy() && !context.Stack().Top().empty()) Throw(Error::SigNullDummy);
    context.Stack().Pop();

    // All signatures were verified if we reached the end of the signature array.
    return sig_pos == sig_count;
  });
}

// Op::CheckSig
static void OnCheckSig(const Context& context) {
  // sig pubkey -- bool
  context.Stack().Call([&](Bytes sigblob, Bytes pkblob) -> bool {
    using namespace crypto::ecdsa;
    Assert(context.env.spend.has_value());

    if (sigblob.empty()) return false;

    // TODO: Tapscript path: EvalChecksigTapscript.
    // TODO: Check signature and pubkey encodings: CheckSignatureEncoding, CheckPubKeyEncoding.

    // Prepare the script code, e.g. by stripping the sigblob instructions from legacy scripts.
    const auto script_code = PrepareScriptCode(context.ScriptCode(), {&sigblob, 1});

    // Create the spend digest, which is a 32-byte hash of transaction bytes committing to the spend.
    const auto digest = BuildSpendDigest(*context.env.spend, sigblob, script_code);

    // Extract the DER-encoded ECDSA signature, which is up to 72 bytes.
    const auto signature = ParseSignatureDER<secp256k1::Wide>(sigblob.first(sigblob.size() - 1), context.DERParsing());
    if (!signature && context.IsStrictDER()) Throw(Error::InvalidDERSignature);

    // The public key is in SEC1 format.
    const auto pubkey = secp256k1::PublicKeyFromSEC1(pkblob);

    // Verify that the spend digest was signed by the private key corresponding to this public key.
    return (pubkey && signature) ? secp256k1::VerifySignature(*pubkey, *signature, digest) : false;
  });
}

void RegisterSigHandlers(Dispatcher& table) {
  table[Op::CheckMultiSig] = &OnCheckMultiSig;
  table[Op::CheckSig] = &OnCheckSig;
}

}  // namespace hornet::protocol::script::runtime
