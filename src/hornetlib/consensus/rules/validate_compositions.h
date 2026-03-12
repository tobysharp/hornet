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

[[nodiscard]] inline Result ValidateBlock(const protocol::Block& block,
                                        const protocol::BlockHeader& parent,
                                        const HeaderAncestryView& view,
                                        const int64_t current_time,
                                        const UnspentOutputsView& unspent) {
  // clang-format off
  static const auto ruleset = std::make_tuple(
    Rule{ValidateHeader,          MakeHeaderContext},
    Rule{ValidateStructural,      MakeEnvironmentContext},
    Rule{ValidateContextual,      MakeEnvironmentContext},
    Rule{ValidateSpending,        MakeBlockSpendContext}
  );
  //clang-format on                                            
  const BlockValidationContext context{block, parent, view, current_time, unspent};
  return ValidateRules(ruleset, view.Length(), context);
}

[[nodiscard]] inline Result ValidateBlockNoScripts(const protocol::Block& block,
                                        const protocol::BlockHeader& parent,
                                        const HeaderAncestryView& view,
                                        const int64_t current_time,
                                        const UnspentOutputsView& unspent) {
  return ValidateBlock(block, parent, view, current_time, unspent);
}

}  // namespace hornet::consensus::rules
