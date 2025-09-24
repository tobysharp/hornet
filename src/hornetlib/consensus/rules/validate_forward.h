#pragma once

#include <variant>

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

using BlockOrTransactionError = std::variant<BlockError, TransactionError>;

namespace rules {

[[nodiscard]] SuccessOr<HeaderError> ValidateHeader(const model::HeaderContext& parent,
                                                 const protocol::BlockHeader& header,
                                                 const HeaderAncestryView& view,
                                                 const int64_t current_time);

[[nodiscard]] SuccessOr<TransactionError> ValidateTransaction(
    const protocol::TransactionConstView transaction);

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] SuccessOr<BlockOrTransactionError> ValidateBlockStructure(const protocol::Block& block);

[[nodiscard]] SuccessOr<BlockError> ValidateBlockContext(const HeaderAncestryView& ancestry,
                                                      const protocol::Block& block);

}  // namespace rules
}  // namespace hornet::consensus
