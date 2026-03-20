#pragma once

#include <optional>
#include <span>

#include "hornetlib/consensus/rules/scripts/spend_patterns.h"
#include "hornetlib/consensus/rules/scripts/verify_flags.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/script/lang/op.h"
#include "hornetlib/protocol/script/view.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/algorithm.h"

namespace hornet::consensus::rules::scripts::sigops {

template <bool kAccurate>
inline int OpCodeCount(protocol::script::lang::Op opcode, protocol::script::lang::Op last) {
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
inline int ScriptCount(protocol::Script script) {
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

inline int WitnessProgramCount(const WitnessProgram& witness, protocol::WitnessView input_stack) {
  // We only know about v0 witness programs.
  if (witness.version != 0) return 0;

  // For a v0 key hash program, count one sig-op.
  constexpr int kV0KeyHashSize = 20;
  if (std::ssize(witness.program) == kV0KeyHashSize) return 1;

  // For a v0 script hash program, count the sig-ops in the final input witness component.
  constexpr int kV0ScriptHashSize = 32;
  if (std::ssize(witness.program) == kV0ScriptHashSize && !input_stack.Empty())
    return ScriptCount<true>(input_stack.Back());

  return 0;
}

inline int SpendPathCost(const SpendScripts& spend, const SpendPath& path) {
  switch (path.type) {
    case SpendPath::P2SH:
      return ScriptCount<true>(*path.redeem) * 4;

    case SpendPath::Witness:
    case SpendPath::P2SHWitness:
      return WitnessProgramCount(*path.witness, spend.witness);

    default:
      return 0;
  }
}

}  // namespace hornet::consensus::rules::scripts::sigops
