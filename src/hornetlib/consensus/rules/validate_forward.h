#pragma once

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus::rules {

using ValidateHeaderResult = ErrorStack<HeaderError>;
using ValidateTransactionResult = ErrorStack<TransactionError>;
using ValidateBlockResult = ErrorStack<std::variant<BlockError, TransactionError>>;

[[nodiscard]] ValidateHeaderResult ValidateHeader(const model::HeaderContext& parent,
                                                           const protocol::BlockHeader& header,
                                                           const HeaderAncestryView& view);

[[nodiscard]] ValidateTransactionResult ValidateTransaction(
    const protocol::TransactionConstView transaction);

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] ValidateBlockResult ValidateBlockStructure(const protocol::Block& block);

[[nodiscard]] ValidateBlockResult ValidateBlockContext(const HeaderAncestryView& ancestry,
                                                              const protocol::Block& block);

}  // namespace hornet::consensus::rules
