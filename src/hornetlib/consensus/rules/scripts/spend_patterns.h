#pragma once

#include <optional>

#include "hornetlib/consensus/rules/scripts/verify_flags.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules::scripts {

struct WitnessProgram {
  const int version;
  const protocol::Script program;

  // A witness program must consist of exactly two instructions: a non-negative constant push opcode, followed by a
  // direct push of between 2 and 40 bytes.
  [[nodiscard]] static std::optional<WitnessProgram> Parse(protocol::Script script) {
    using namespace protocol::script::lang;

    // The script size must be between 4 and 42 bytes.
    const int script_size = std::ssize(script);
    if (script_size < 4 || script_size > 42) return std::nullopt;

    // The first opcode must be one of {OP_0, OP_1, ..., OP_16}.
    const Op version_op{script[0]};
    if (!IsConstantPush(version_op) || OpToConstant(version_op) < 0) return std::nullopt;

    // The second opcode must be a direct push instruction.
    const Op push_op{script[1]};
    if (!IsDirectPush(push_op)) return std::nullopt;

    // The direct-push payload must exactly match the remaining script bytes.
    if (DirectPushSize(push_op) != script_size - 2) return std::nullopt;

    return {{OpToConstant(version_op), script.subspan(2)}};
  }
};

// A P2SH script must be 23 bytes long consisting exactly of: {Op::Hash160, 0x14, <20 bytes of data>, Op::Equal}.
// Informally, this hashes the top-of-stack and compares the result against the data in the script.
[[nodiscard]] inline bool IsPayToScriptHash(protocol::Script script) {
  using protocol::script::lang::Op;
  return script.size() == 23u && script[0] == +Op::Hash160 && script[1] == 0x14 && script[22] == +Op::Equal;
}

// Extracts the redeem script from a push-only scriptSig. In P2SH spends, the redeem script is encoded as the last
// pushed element in scriptSig. Returning it lets the caller recover the script committed to by the scriptPubKey.
[[nodiscard]] inline std::optional<protocol::Script> ExtractRedeemScript(protocol::Script script_sig) {
  if (script_sig.empty()) return std::nullopt;

  protocol::Script last;
  protocol::script::Parser parser{script_sig};
  while (!parser.IsEof()) {
    const auto instruction = parser.Next();
    if (!instruction || !IsPush(instruction->opcode)) return std::nullopt;
    last = instruction->data;
  }
  return last;
}

struct SpendScripts {
  protocol::Script pubkey, sig;
  protocol::WitnessView witness;
  uint64_t flags;
};

struct SpendPath {
  enum Type { Legacy, P2SH, Witness, P2SHWitness };
  Type type = Legacy;
  std::optional<protocol::Script> redeem = {};
  std::optional<WitnessProgram> witness = {};

  [[nodiscard]] inline static SpendPath Classify(const SpendScripts& spend) {
    // Native witness path
    if (IsFlag(spend.flags, VerifyFlag::Witness)) {
      if (const auto witness = WitnessProgram::Parse(spend.pubkey)) return {Witness, {}, witness};
    }

    // P2SH path
    if (IsFlag(spend.flags, VerifyFlag::P2SH) && IsPayToScriptHash(spend.pubkey)) {
      if (const auto redeem = ExtractRedeemScript(spend.sig)) {
        // P2SHWitness path
        if (IsFlag(spend.flags, VerifyFlag::Witness)) {
          if (const auto witness = WitnessProgram::Parse(*redeem)) return {P2SHWitness, redeem, witness};
        }
        return {P2SH, redeem};
      }
    }

    // Legacy path
    return {Legacy};
  }
};

}  // namespace hornet::consensus::rules::scripts
