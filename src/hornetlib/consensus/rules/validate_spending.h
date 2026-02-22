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
  const SpendRecord spend;
  const int height;
};

// Coinbase outputs MUST NOT be spent until 100 blocks after their creation.
[[nodiscard]] inline Result ValidateCoinbaseMaturity(const InputSpendingContext& context) {
  constexpr int kCoinbaseMaturity = 100;  // Number of blocks until coinbase maturity.

  if (context.spend.IsCoinbase() && context.height - context.spend.funding_height < kCoinbaseMaturity)
    return Error::Transaction_PrematureSpend;

  return {};
}

}  // namespace hornet::consensus::rules
