#include <optional>
#include <span>

#include "hornetlib/consensus/rules/scripts/sigops.h"
#include "hornetlib/consensus/rules/scripts/sigops_detail.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/script/satisfy.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules::scripts {

// Returns the sum of sigop counts across all legacy input sig scripts and output pubkey scripts.
int LegacySigOpCount(protocol::TransactionConstView tx) {
  /* mutable */ int sum = 0;
  for (const auto& script : tx.SignatureScripts()) sum += sigops::ScriptCount(script);
  for (const auto& script : tx.PkScripts()) sum += sigops::ScriptCount(script);
  return sum;
}

// Returns the sum of sigop costs for a transaction, given spending information.
int SigOpCost(protocol::TransactionConstView tx, std::span<const SpendRecord> spends, protocol::script::FeatureFlags flags) {
  using namespace scripts;
  using protocol::script::Feature;
  constexpr int kWitnessScale = 4;
  Assert(!flags.Has(Feature::Witness) || flags.Has(Feature::P2SH));
  Assert(tx.InputCount() == std::ssize(spends));

  // Count the number of sig-ops in legacy pubkey and sig scripts.
  const int legacy_cost = LegacySigOpCount(tx) * kWitnessScale;

  // Coin-base transactions only contribute legacy sig-ops.
  if (tx.IsCoinBase()) return legacy_cost;

  // Counts the cost of P2SH and witness sig-ops across all inputs.
  const int spend_path_cost = util::Sum(0, tx.InputCount(), [&](int i) {
    const SpendScripts spend{spends[i].pubkey_script, tx.SignatureScript(i), tx.InputWitness(i), flags};
    return sigops::SpendPathCost(spend, SpendPath::Classify(spend));
  });

  // Scale the non-witness sig-ops by a factor of 4, and sum for the total transaction sig-op cost.
  return legacy_cost + spend_path_cost;
}

}  // namespace hornet::consensus::rules::scripts
