#pragma once

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/rules.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

[[nodiscard]] inline Result ValidateHeader(const protocol::BlockHeader& header,
                                           const protocol::BlockHeader& parent,
                                           const HeaderAncestryView& view,
                                           const int64_t current_time) {
  return Validate(kHeaderRules,
      HeaderValidationContext{header, parent, view, current_time, view.Length()});
}

[[nodiscard]] inline Result ValidateTransaction(const protocol::TransactionConstView tx) {
  return Validate(kTransactionRules, tx);
}

[[nodiscard]] inline Result ValidateSpendingTransaction(const TransactionSpendContext& context) {
  return Validate(kSpendingTransactionRules, context);
}

[[nodiscard]] inline Result ValidateSpending(const BlockSpendContext& context) {
  return Validate(kSpendingRules, context);
}

[[nodiscard]] inline Result ValidateLocal(const protocol::Block& block) {
  return Validate(kLocalRules, block);
}

[[nodiscard]] inline Result ValidateWitness(const BlockEnvironmentContext& context) {
  return Validate(kWitnessRules, MakeWitnessContext(context));
}

[[nodiscard]] inline Result ValidateContextual(const BlockEnvironmentContext& context) {
  using namespace rules;
  return Validate(kContextualRules, context);
}

// Block Validation Rules
[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                        const protocol::BlockHeader& parent,
                                        const HeaderAncestryView& view,
                                        const int64_t current_time,
                                        const UnspentOutputsView& unspent) {
  const BlockValidationContext context{block, parent, view, current_time, unspent};
  return Validate(kBlockRules, context);
}

}  // namespace hornet::consensus::rules
