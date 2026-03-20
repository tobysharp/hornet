#include <optional>
#include <span>

#include "hornetlib/consensus/rules/scripts/sigops.h"
#include "hornetlib/consensus/rules/scripts/sigops_detail.h"
#include "hornetlib/consensus/rules/scripts/verify_flags.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules::sigops {

int LegacyCount(protocol::TransactionConstView tx) {
  /* mutable */ int sum = 0;
  for (const auto& script : tx.SignatureScripts()) sum += detail::ScriptCount(script);
  for (const auto& script : tx.PkScripts()) sum += detail::ScriptCount(script);
  return sum;
}

int TotalCost(protocol::TransactionConstView tx, std::span<const SpendRecord> spends, uint64_t flags) {
  using namespace scripts;
  constexpr int kWitnessScale = 4;
  Assert(!IsFlag(flags, VerifyFlag::Witness) || IsFlag(flags, VerifyFlag::P2SH));
  Assert(tx.InputCount() == std::ssize(spends));

  // Count the number of sig-ops in legacy pubkey and sig scripts.
  const int legacy_sig_ops = LegacyCount(tx);

  // Coin-base transactions only contribute legacy sig-ops.
  if (tx.IsCoinBase()) return legacy_sig_ops * kWitnessScale;

  // Count the number of P2SH sig-ops if the P2SH flag is enabled.
  const int p2sh_sig_ops = detail::P2SHCount(tx, spends, flags);

  // Count the number of witness sig-ops across all inputs.
  const int witness_sig_ops = util::Sum(0, tx.InputCount(), [&](int i) {
    return detail::WitnessCount(tx.SignatureScript(i), spends[i].pubkey_script, tx.InputWitness(i), flags);
  });

  // Scale the non-witness sig-ops by a factor of 4, and sum for the total transaction sig-op cost.
  return (legacy_sig_ops + p2sh_sig_ops) * kWitnessScale + witness_sig_ops;
}

}  // namespace hornet::consensus::rules::sigops
