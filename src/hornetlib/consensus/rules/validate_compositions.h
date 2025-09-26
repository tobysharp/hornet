#pragma once

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rule.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/validate.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"

namespace hornet::consensus::rules {

// Performs non-spending validation, aligned with the combination of Core's CheckBlock and ContextualCheckBlock functions.
[[nodiscard]] inline Result ValidateNonSpending(const BlockEnvironmentContext& context) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateStructural},           
    Rule{ValidateContextual}
  );
  //clang-format on
  return ValidateRules(ruleset, context.height, context);
}

[[nodiscard]] inline Result ValidateSpending(const BlockSpendingContext& context) {
  return context.unspent.ForEachUnspentPrevout(context.block,
    [&](const int tx_index, const int input_index, const UnspentDetail& prevout) {
      return ValidateInputSpend(context.block.Transaction(tx_index), input_index, prevout, context.height);
  });
}

[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                        const protocol::BlockHeader& parent,
                                        const HeaderAncestryView& view,
                                        const int64_t current_time,
                                        const UnspentTransactionsView& unspent) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateHeader,          MakeHeaderContext},
    Rule{ValidateNonSpending,     MakeEnvironmentContext},
    Rule{ValidateSpending,        MakeBlockSpendingContext}
  );
  //clang-format on                                            
  const BlockValidationContext context{block, parent, view, current_time, unspent};
  return ValidateRules(ruleset, view.Length(), context);
}

}  // namespace hornet::consensus::rules
