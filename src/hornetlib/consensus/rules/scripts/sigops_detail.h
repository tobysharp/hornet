#pragma once

#include <optional>
#include <span>

#include "hornetlib/consensus/rules/scripts/patterns.h"
#include "hornetlib/consensus/rules/scripts/verify_flags.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/algorithm.h"

namespace hornet::consensus::rules::sigops::detail {

template <bool kAccurate>
int OpCodeCount(protocol::script::lang::Op opcode, protocol::script::lang::Op last) {
  using namespace protocol::script::lang;
  constexpr int kMaxPubKeysPerMultisig = 20;

  switch (opcode) {
    case Op::CheckSig:
    case Op::CheckSigVerify:
      return 1;
    case Op::CheckMultiSig:
    case Op::CheckMultiSigVerify:
      return (kAccurate && last >= Op::PushConst1 && last <= Op::PushConst16) ? OpToConstant(last)
                                                                              : kMaxPubKeysPerMultisig;
    default:
      return 0;
  }
}

template <bool kAccurate = false>
int ScriptCount(protocol::Script script) {
  using namespace protocol::script::lang;

  // Return the sum of all sig-op counts for each instruction in the script.
  /* mutable */ int sum = 0;
  /* mutable */ Op last_opcode = Op::PushEmpty;
  for (const auto& instruction : protocol::script::View{script}.Instructions()) {
    sum += OpCodeCount<kAccurate>(instruction.opcode, last_opcode);
    last_opcode = instruction.opcode;
  }
  return sum;
}

int P2SHCount(protocol::TransactionConstView tx, std::span<const SpendRecord> spends, uint64_t flags) {
  Assert(!tx.IsCoinBase());
  if (!scripts::IsFlag(flags, scripts::VerifyFlag::P2SH)) return 0;

  // Sums the sig-op counts of redeem scripts from all P2SH spends in the transaction.
  return util::Sum(0, tx.InputCount(), [&](int i) {
    if (scripts::IsPayToScriptHash(spends[i].pubkey_script))
      if (const auto redeem = scripts::ExtractRedeemScript(tx.SignatureScript(i))) return ScriptCount<true>(*redeem);
    return 0;
  });
}

std::optional<int> WitnessProgramCount(protocol::Script script, protocol::WitnessView input_stack) {
  const auto witness = scripts::WitnessProgram::Parse(script);
  if (!witness) return std::nullopt;

  // We only know about v0 witness programs.
  if (witness->version != 0) return 0;

  // For a v0 key hash program, count one sig-op.
  constexpr int kV0KeyHashSize = 20;
  if (std::ssize(witness->program) == kV0KeyHashSize) return 1;

  // For a v0 script hash program, count the sig-ops in the final input witness component.
  constexpr int kV0ScriptHashSize = 32;
  if (std::ssize(witness->program) == kV0ScriptHashSize && !input_stack.Empty())
    return ScriptCount<true>(input_stack.Back());

  return 0;
}

// Returns the number of witness sig-ops chargeable during script validation under the given flags.
int WitnessCount(protocol::Script script_sig, protocol::Script script_pub_key, protocol::WitnessView input_stack,
                 uint64_t flags) {
  using namespace scripts;

  // If we are not verifying witness scripts, return zero.
  if (!IsFlag(flags, VerifyFlag::Witness)) {
    return 0;
  }
  // If the scriptPubKey contains a witness program, count its sig-ops.
  else if (const auto witness_ops = WitnessProgramCount(script_pub_key, input_stack)) {
    return *witness_ops;
  }
  // Or if the scriptPubKey is P2SH, extract the redeem script's witness program, and count its sig-ops.
  else if (IsPayToScriptHash(script_pub_key)) {  // P2SH
    if (const auto redeem_script = ExtractRedeemScript(script_sig)) {
      if (const auto witness_ops = WitnessProgramCount(*redeem_script, input_stack)) {
        return *witness_ops;
      }
    }
  }
  return 0;
}

}  // namespace hornet::consensus::rules::sigops::detail
