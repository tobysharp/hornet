#pragma once

#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

struct BlockSpendingContext {
  const protocol::Block& block;
  const UnspentOutputsView& unspent;
  const int height;
};

inline BlockSpendingContext MakeBlockSpendingContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.unspent, rhs.view.Length()};
}

struct InputSpendingContext {
  const protocol::TransactionConstView tx;
  std::span<const SpendRecord> spends;
  const int height;
};

// Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
[[nodiscard]] inline Result ValidateCoinbaseMaturity(const InputSpendingContext& context) {
  constexpr int kCoinbaseMaturity = 100;  // Number of blocks until coinbase maturity.

  for (const SpendRecord& spend : context.spends) {
    if (spend.IsCoinbase() && context.height - spend.funding_height < kCoinbaseMaturity)
      return Error::Input_PrematureSpend;
  }

  return {};
}

// Each spend amount MUST be non-negative.
[[nodiscard]] inline Result ValidateInputAmountsNonNegative(const InputSpendingContext& context) {
  if (std::ranges::any_of(context.spends,
                          [](const SpendRecord& spend) { return spend.amount < 0; }))
    return Error::Input_InvalidAmount;  
    
  return {};
}

// The sum of all spend amounts MUST NOT exceed the money supply limit.
[[nodiscard]] inline Result ValidateInputAmountsSum(const InputSpendingContext& context) {
  constexpr int64_t kSatoshisPerBitcoin = 100'000'000;
  constexpr int64_t kMoneySupplyLimit = 21'000'000 * kSatoshisPerBitcoin;

  if (std::reduce(context.spends.begin(), context.spends.end(), 0ll,
                  [](int64_t sum, const SpendRecord& spend) { return sum + spend.amount; }) >
      kMoneySupplyLimit)
    return Error::Input_InvalidAmount;

  return {};
}

// The locking script concatenated with the sig script MUST evaluate to TRUE.
[[nodiscard]] inline Result ValidateScripts(const InputSpendingContext&) {
  return {};
}

}  // namespace hornet::consensus::rules
