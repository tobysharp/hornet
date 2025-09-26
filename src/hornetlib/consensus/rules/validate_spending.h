#pragma once

#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

struct BlockSpendingContext {
  const protocol::Block& block;
  const UnspentTransactionsView& unspent;
  const int height;
};

inline BlockSpendingContext MakeBlockSpendingContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.unspent, rhs.view.Length()};
}

struct InputSpendingContext {
    const protocol::TransactionConstView tx;
    const int input_index;
    const UnspentDetail& prevout;
    const int height;
};

[[nodiscard]] inline Result ValidateCoinbaseMaturity(const InputSpendingContext&) {
    return {};  // TODO
}

}  // namespace hornet::consensus::rules
