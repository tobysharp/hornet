#pragma once

#include <array>
#include <tuple>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/scripts/patterns.h"
#include "hornetlib/consensus/rules/scripts/sigops.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/spend.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/algorithm.h"

namespace hornet::consensus::rules {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spending validation rules per input
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Coinbase outputs MUST NOT be spent before 100 blocks after their creation.
[[nodiscard]] inline Result ValidateCoinbaseMaturity(const InputSpendContext& context) {
  constexpr int kCoinbaseMaturity = 100;  // Number of blocks until coinbase maturity.

  if (context.spend.IsCoinbase() && context.height - context.spend.funding_height < kCoinbaseMaturity)
    return Error::Spending_PrematureSpend;

  return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spending validation rules per transaction
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.
[[nodiscard]] inline Result ValidateOutputsAtMostInputs(const TransactionSpendContext& context) {
  const int64_t outputs_sum = util::Sum(context.tx.Outputs(), [](const auto& output) { return output.value; });
  const int64_t inputs_sum = util::Sum(context.spends, [](const auto& spend) { return spend.amount; });

  return (outputs_sum > inputs_sum) ? Error::Spending_OutputAmountsExceedInputAmounts : Result::Ok;
}

// From BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.
[[nodiscard]] inline Result /* [[BIP::SequenceLocks]] */ ValidateSequenceLocks(const TransactionSpendContext& context) {
  if (context.tx.Version() < 2) return {};  // BIP68 applies only to transactions with version >= 2.

  constexpr uint32_t kDisableMask = 1u << 31;
  constexpr uint32_t kDeltaMask = 0xffff;
  constexpr uint32_t kTimestampsMask = 1u << 22;
  constexpr int kTimestampsShift = 9;

  int min_valid_height = 0;   // The minimum height at which finality is achieved for this transaction.
  int64_t min_valid_mtp = 0;  // The minimum MTP time at which finality is achieved.

  for (const auto& spend : context.spends) {
    const protocol::Input& input = context.tx.Input(spend.spend_input_index);

    // Sequence numbers with the most significant bit set do not participate in this consensus rule.
    if (input.sequence & kDisableMask) continue;

    // The low 16 bits of nSequence store the number of temporal units until validity.
    const int delta = input.sequence & kDeltaMask;

    // If bit 22 is set, the sequence variable is interpreted as time-based, otherwise it is height-based.
    if (input.sequence & kTimestampsMask) {
      // The time origin is defined as the Median Time Past of the block prior to the funding transaction.
      const uint32_t origin_time = context.ancestry.MedianTimePast(spend.funding_height - 1);
      // The time delta until finality is 512 seconds for each encoded unit.
      min_valid_mtp = std::max(min_valid_mtp, int64_t{origin_time} + (delta << kTimestampsShift));
    } else {
      // nSequence is simply the number of blocks until finality.
      min_valid_height = std::max(min_valid_height, spend.funding_height + delta);
    }
  }

  // The current time is taken to be the MTP of the parent block.
  const int64_t parent_mtp = context.ancestry.MedianTimePast();

  // Return error if finality was not achieved.
  if (context.height < min_valid_height || parent_mtp < min_valid_mtp) return Error::Spending_NonFinalTransaction;

  return {};
}

// A non-coinbase input MUST satisfy the spent output's locking script.
[[nodiscard]] inline Result ValidateScripts(const TransactionSpendContext& context) {
  if (context.tx.IsCoinBase()) return {};

  Assert(context.tx.InputCount() == std::ssize(context.spends));

  // Core first checks in a script execution result cache.
  // Next it precomputes the SHA256 hash of various quantities depending on whether the tx uses BIP143 or BIP341.
  // Then the actual execution is in the VerifyScript / EvalScript functions.

  // const uint64_t flags = 0;  // TODO
  // const bool require_minimal = false;  // Not used for validating blocks.

  // We need the script flags. These are determined by block hash and height in GetBlockScriptFlags.
  //    - Generally p2sh, witness, and taproot. By activation height, dersig (BIP66), checklocktimeverify (BIP65),
  //    checksequenceverify (BIP112), and nulldummy (BIP147).

  for (int i = 0; i < context.tx.InputCount(); ++i) {
    const SpendRecord& record = context.spends[i];
    Assert(record.spend_input_index == i);  // If this isn't always true, I need to understand why not. If it is, then
                                           // the input_index field can be removed from SpendRecord.

    Assert(!scripts::IsPayToScriptHash(record.pubkey_script));
    Assert(!scripts::WitnessProgram::Parse(record.pubkey_script));

    const bool is_strict_der_signatures = IsBIPActiveAtHeight(BIP::StrictDERSignatures, context.height);
  
    // We execute the unlocking script (scriptSig) followed by the locking script (scriptPubKey) sequentially using the
    // same stack to prevent script concatenation attacks. (See CVE-2010-5141.)
    protocol::script::SpendContext spend = { context.tx, record.spend_input_index, protocol::script::SpendPath::LegacyDirect };
    protocol::script::runtime::Policy policy = { false, is_strict_der_signatures };
    protocol::script::Processor processor{policy, context.height, std::make_optional(spend)};
    if (const auto unlock_result = processor.Run(context.tx.SignatureScript(i)); !unlock_result) 
      return Error::Spending_ScriptLocked;  // Looks like this tx input script is inherently invalid.
    const auto lock_result = processor.Run(record.pubkey_script);
    if (!lock_result || !*lock_result) return Error::Spending_ScriptLocked;

    // TODO: Witness programs...
    // TODO: P2SH scripts...
    // TODO: Cleanstack check only for policy not consensus
    // TODO: Witness => P2SH check
  }

  return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spending validation rules per block
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline uint64_t GetScriptVerifyFlags(const BlockValidationContext& rhs) {
  using namespace scripts;
  const auto exception_hash = "00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22"_sha256;
  return rhs.block.Header().ComputeHash() == exception_hash ? 0 : CombineFlags({VerifyFlag::P2SH, VerifyFlag::Witness});
}

inline BlockSpendContext MakeBlockSpendContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.view, rhs.unspent, rhs.view.Length(), GetScriptVerifyFlags(rhs)};
}

// A transaction input MUST reference a previous transaction output that remains unspent.
[[nodiscard]] inline Result ValidateInputPrevoutsUnspent(const BlockSpendContext& context) {
  return context.unspent.QueryPrevoutsUnspent(context.block);
}

// Transaction outputs MUST NOT give rise to duplicates of existing unspent outpoints (BIP30).
[[nodiscard]] inline Result ValidateOutPointsUnique(const BlockSpendContext& context) {
  // Skip this rule for two specific historical blocks that are known to violate it.
  static constexpr auto kKnownExceptions =
      std::array{std::tuple{91842, "00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec"_sha256},
                 std::tuple{91880, "00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721"_sha256}};
  const auto hash = context.block.Header().ComputeHash();
  for (const auto& known : kKnownExceptions) {
    if (context.height == std::get<int>(known) && hash == std::get<protocol::Hash>(known)) return {};
  }

  // Note that we can apply an optimization to skip this check for a common case where certain constraints hold.
  // Here we have to guarantee that (1) no new duplicates can be created, and (2) there are no unspent duplicates
  // remaining in the chain history. If we are on the familiar mainnet chain post-BIP34 activation, these constraints
  // are known to hold before height 1,983,702, since that integer does exist in a pre-BIP34 coinbase script.
  static const int kBIP34Height = GetSoftForkActivationHeight(BIP::HeightInCoinbase);
  if (context.height > kBIP34Height &&
      context.ancestry.HashAt(kBIP34Height) ==
          "000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8"_sha256 &&
      context.height < 1'983'702)
    return {};

  // In all other cases, we must validate that no transaction creates a duplicate UTXO.
  return context.unspent.QueryOutPointsUnique(context.block);
}

// The total signature-operation cost over all transactions MUST NOT exceed 80,000.
[[nodiscard]] inline Result ValidateSigOpCosts(const BlockSpendContext& context) {
  constexpr int kMaxBlockSigOpCost = 80'000;
  int sigops_cost = 0;
  const auto spends = context.unspent.Spends(context.block);
  if (!spends) return {};  // Failed precondition.
  for (const JoinedSpend spending : *spends) {
    Assert(spending.tx.InputCount() == std::ssize(spending.spends));
    sigops_cost += scripts::SigOpCost(spending.tx, spending.spends, context.script_flags);
    if (sigops_cost > kMaxBlockSigOpCost) return Error::Spending_BadSigOpsCost;
  }
  return {};
}

// The total amount in coinbase outputs MUST NOT exceed the block reward.
[[nodiscard]] inline Result ValidateBlockSubsidy(const BlockSpendContext& context) {
  constexpr int64_t kSatoshisPerCoin = 100'000'000;
  constexpr int64_t kInitialBlockReward = 50 * kSatoshisPerCoin;
  constexpr int kBlocksPerEpoch = 210'000;
  using protocol::TransactionConstView;
  using util::Sum;

  // Computes the block subsidy.
  const int epoch = context.height / kBlocksPerEpoch;
  const int64_t block_subsidy = epoch < 64 ? (kInitialBlockReward >> epoch) : 0;

  // Computes the total of all transaction inputs for the block.
  const auto spends = context.unspent.Spends(context.block);
  if (!spends) return {};  // Failed precondition.
  const int64_t inputs_total = Sum(*spends, [&](const JoinedSpend spending) {
    return Sum(spending.spends, [](const SpendRecord& spend) { return spend.amount; });
  });

  // Computes the total of all non-coinbase transaction outputs for the block.
  const int64_t outputs_total =
      Sum(context.block.Transactions(), [](const auto& tx) { return tx.IsCoinBase() ? 0ll : tx.TotalOutputValue(); });

  // Computes the maximum block reward available for the current block.
  // Assert(*inputs_total >= outputs_total);
  const int64_t fees_amount = inputs_total - outputs_total;
  const int64_t block_reward = block_subsidy + fees_amount;
  const int64_t coinbase_amount = context.block.Transaction(0).TotalOutputValue();

  // Validates that the coinbase outputs don't exceed the available block reward.
  if (coinbase_amount > block_reward) return Error::Spending_CoinbaseAmountExceedsBlockReward;

  return {};
}

}  // namespace hornet::consensus::rules
