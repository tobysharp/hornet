#pragma once

#include <array>
#include <tuple>

#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
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

[[nodiscard]] inline Result ValidateSpendingTransaction(const protocol::TransactionConstView& tx, std::span<const SpendRecord> spends, int height) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateSpendingInputs},
    Rule{ValidateOutputValuesAtMostInputValues}
  );
  //clang-format on
  const TransactionSpendContext context{tx, spends, height};
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
};

inline BlockSpendContext MakeBlockSpendContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.view, rhs.unspent, rhs.view.Length()};
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
      return ValidateSpendingTransaction(tx, spends, context.height);
    });
}

[[nodiscard]] inline Result ValidateSpending(const BlockSpendContext& context) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateOutPointsUnique},
    Rule{ValidateInputPrevoutsUnspent},
    Rule{ValidateSpendingTransactions}
  );
  // clang-format on
  return ValidateRules(ruleset, context.height, context);
}

}  // namespace hornet::consensus::rules
