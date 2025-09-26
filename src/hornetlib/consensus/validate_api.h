#pragma once

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/rules/validate.h"

namespace hornet::consensus {

// Export the top-level validation functions to the hornet::consensus namespace.
using rules::ValidateHeader;
using rules::ValidateTransaction;
using rules::ValidateStructural;
using rules::ValidateContextual;
using rules::ValidateNonSpending;
using rules::ValidateSpending;
using rules::ValidateBlock;

[[nodiscard]] inline Result ValidateHeader(
                                const protocol::BlockHeader& header,
                                const protocol::BlockHeader& parent,
                                const HeaderAncestryView& view,
                                const int64_t current_time) {
  return rules::ValidateHeader(HeaderValidationContext{header, parent, view, current_time, view.Length()});
}

[[nodiscard]] inline Result ValidateContextual(
                                const protocol::Block& block,
                                const HeaderAncestryView& view) {
  return rules::ValidateContextual(BlockEnvironmentContext{block, view, view.Length()});
}

[[nodiscard]] inline Result ValidateNonSpending(
                                const protocol::Block& block,
                                const HeaderAncestryView& view) {
  return rules::ValidateNonSpending(BlockEnvironmentContext{block, view, view.Length()});
}

[[nodiscard]] inline Result ValidateSpending(  
                                const protocol::Block& block,
                                const UnspentTransactionsView& unspent,
                                const int height) {
  return rules::ValidateSpending(BlockSpendingContext{block, unspent, height});
}

}  // namespace hornet::consensus