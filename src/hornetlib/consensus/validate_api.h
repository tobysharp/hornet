#pragma once

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/context.h"
#include "hornetlib/consensus/rules/validate.h"
#include "hornetlib/consensus/rules/validate_compositions.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"

namespace hornet::consensus {

// Export the top-level validation functions to the hornet::consensus namespace.
using rules::ValidateContextual;
using rules::ValidateStructural;
using rules::ValidateTransaction;

[[nodiscard]] inline Result ValidateHeader(const protocol::BlockHeader& header,
                                           const protocol::BlockHeader& parent,
                                           const HeaderAncestryView& view,
                                           const int64_t current_time) {
  return rules::ValidateHeader(
      rules::HeaderValidationContext{header, parent, view, current_time, view.Length()});
}

[[nodiscard]] inline Result ValidateContextual(const protocol::Block& block,
                                               const HeaderAncestryView& view) {
  return rules::ValidateContextual(rules::BlockEnvironmentContext{block, view, view.Length()});
}

[[nodiscard]] inline Result ValidateNonSpending(const protocol::Block& block,
                                                const HeaderAncestryView& view) {
  return rules::ValidateNonSpending(rules::BlockEnvironmentContext{block, view, view.Length()});
}

[[nodiscard]] inline Result ValidateSpending(const protocol::Block& block,
                                             const UnspentOutputsView& unspent, const int height) {
  return rules::ValidateSpending(rules::BlockSpendingContext{block, unspent, height});
}

[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                          const protocol::BlockHeader& parent,
                                          const HeaderAncestryView& view,
                                          const int64_t current_time,
                                          const UnspentOutputsView& unspent) {
  return rules::ValidateBlock(block, parent, view, current_time, unspent);
}

}  // namespace hornet::consensus
