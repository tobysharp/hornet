#pragma once

#include <array>
#include <tuple>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/scripts/sigops.h"
#include "hornetlib/consensus/rules/scripts/spend_patterns.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spending validation rules per input
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct InputSpendContext {
  const protocol::TransactionConstView tx;
  const SpendRecord& spend;
  const int height;
};

// Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
[[nodiscard]] inline Result ValidateCoinbaseMaturity(const InputSpendContext& context) {
  constexpr int kCoinbaseMaturity = 100;  // Number of blocks until coinbase maturity.

  if (context.spend.IsCoinbase() && context.height - context.spend.funding_height < kCoinbaseMaturity)
    return Error::Spending_PrematureSpend;

  return {};
}

[[nodiscard]] inline Result ValidateSpendingInput(const protocol::TransactionConstView& tx, const SpendRecord& spend,
                                                  int height) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateCoinbaseMaturity}         // Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
  );
  //clang-format on
  const InputSpendContext context{tx, spend, height};
  return ValidateRules(ruleset, height, context);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spending validation rules per transaction
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct TransactionSpendContext {
  const protocol::TransactionConstView tx;
  std::span<const SpendRecord> spends;
  const HeaderAncestryView& ancestry;
  const int height;
  const int64_t inputs_sum = std::reduce(spends.begin(), spends.end(), 0ll, [](int64_t sum, const auto& spend) { return sum + spend.amount; });
  const int64_t outputs_sum = std::reduce(tx.Outputs().begin(), tx.Outputs().end(), 0ll, [](int64_t sum, const auto& output) { return sum + output.value; });
};

[[nodiscard]] inline Result ValidateSpendingInputs(const TransactionSpendContext& context) {
  for (const auto& spend : context.spends) {
    if (Result result = ValidateSpendingInput(context.tx, spend, context.height); !result) return result;
  }
  return {};
}

// The sum of output values in a transaction MUST NOT exceed the sum of all input values being spent.
[[nodiscard]] inline Result ValidateOutputValuesAtMostInputValues(const TransactionSpendContext& context) {
  if (context.outputs_sum > context.inputs_sum) return Error::Spending_OutputAmountsExceedInputAmounts;

  return {};
}

// BIP68: Each input that signals a relative lock-time interval MUST have reached relative finality.
[[nodiscard]] inline Result ValidateSequenceLocks(const TransactionSpendContext& context) {
  if (context.tx.Version() < 2) return {};  // BIP68 applies only to transactions with version >= 2.

  constexpr uint32_t kDisableMask = 1u << 31;
  constexpr uint32_t kDeltaMask = 0xffff;
  constexpr uint32_t kTimestampsMask = 1u << 22;
  constexpr int      kTimestampsShift = 9;

  int min_valid_height = 0;    // The minimum height at which finality is achieved for this transaction.
  int64_t min_valid_mtp = 0;   // The minimum MTP time at which finality is achieved.

  for (int input_index = 0; input_index < context.tx.InputCount(); ++input_index) {
    const protocol::Input& input = context.tx.Input(input_index);
    const SpendRecord& spend = context.spends[input_index];
    
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

[[nodiscard]] inline Result ValidateSpendingTransaction(const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends, const HeaderAncestryView& ancestry, int height) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateSpendingInputs},
    Rule{ValidateOutputValuesAtMostInputValues},
    Rule{ValidateSequenceLocks, BIP::SequenceLocks}
    // TODO: Transaction sig-op costs rule
    // TODO: Input scripts rule
    // TODO: Coinbase amount <= block subsidy + fees
  );
  //clang-format on
  const TransactionSpendContext context{tx, spends, ancestry, height};
  return ValidateRules(ruleset, height, context);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Spending validation rules per block
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BlockSpendContext {
  const protocol::Block& block;
  const HeaderAncestryView& ancestry;
  const UnspentOutputsView& unspent;
  const int height;
  const uint64_t script_flags;
};

inline uint64_t GetScriptVerifyFlags(const BlockValidationContext& rhs) {
  using namespace scripts;
  const auto exception_hash = "00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22"_sha256;
  return rhs.block.Header().ComputeHash() == exception_hash ? 0 : CombineFlags({VerifyFlag::P2SH, VerifyFlag::Witness});
}

inline BlockSpendContext MakeBlockSpendContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.view, rhs.unspent, rhs.view.Length(), GetScriptVerifyFlags(rhs)};
}

// For every transaction input spending a previous transaction output, that output MUST exist and be unspent.
[[nodiscard]] inline Result ValidateInputPrevoutsUnspent(const BlockSpendContext& context) {
  return context.unspent.QueryPrevoutsUnspent(context.block);
}

// BIP30: Transaction output identifiers MUST NOT collide with those of existing unspent outputs.
[[nodiscard]] inline Result ValidateOutPointsUnique(const BlockSpendContext& context) {
  // Skip this rule for two specific historical blocks that are known to violate it.
  static constexpr auto kKnownExceptions = std::array{
    std::tuple{91842, "00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec"_sha256}, 
    std::tuple{91880, "00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721"_sha256}
  };
  const auto hash = context.block.Header().ComputeHash();
  for (const auto& known : kKnownExceptions) {
    if (context.height == std::get<int>(known) && hash == std::get<protocol::Hash>(known))
      return {};
  }

  // Note that we can apply an optimization to skip this check for a common case where certain constraints hold.
  // Here we have to guarantee that (1) no new duplicates can be created, and (2) there are no unspent duplicates
  // remaining in the chain history. If we are on the familiar mainnet chain post-BIP34 activation, these constraints
  // are known to hold before height 1,983,702, since that integer does exist in a pre-BIP34 coinbase script.
  static const int kBIP34Height = GetSoftForkActivationHeight(BIP::HeightInCoinbase);
  if (context.height > kBIP34Height &&
      context.ancestry.HashAt(kBIP34Height) == "000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8"_sha256 &&
      context.height < 1'983'702)
    return {};

  // In all other cases, we must validate that no transaction creates a duplicate UTXO.
  return context.unspent.QueryOutPointsUnique(context.block);
}

[[nodiscard]] inline Result ValidateSpendingTransactions(const BlockSpendContext& context) {
  return context.unspent.ForEachTransaction(context.block,
    [&](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends) { 
      Assert(tx.InputCount() == std::ssize(spends));
      return ValidateSpendingTransaction(tx, spends, context.ancestry, context.height);
    });
}

// The sum of sigop costs across all transactions MUST NOT exceed the maximum of 80,000.
[[nodiscard]] inline Result ValidateSigOpCosts(const BlockSpendContext& context) {
  constexpr int kMaxBlockSigOpCost = 80'000;
  int sigops_cost = 0;
  return context.unspent.ForEachTransaction(context.block,
    [&](const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends) { 
      Assert(tx.InputCount() == std::ssize(spends));
      sigops_cost += scripts::SigOpCost(tx, spends, context.script_flags);
      return sigops_cost > kMaxBlockSigOpCost ? Error::Spending_BadSigOpsCost : Result::Ok;
  });
}

[[nodiscard]] inline Result ValidateSpending(const BlockSpendContext& context) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateOutPointsUnique},
    Rule{ValidateInputPrevoutsUnspent},
    Rule{ValidateSpendingTransactions},
    Rule{ValidateSigOpCosts}
  );
  // clang-format on
  return ValidateRules(ruleset, context.height, context);
}

}  // namespace hornet::consensus::rules
