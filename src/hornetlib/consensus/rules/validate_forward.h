#pragma once

#include <cstdint>
#include <variant>

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

struct UnspentDetail;

struct BlockValidationContext {
  const protocol::Block& block;
  const protocol::BlockHeader& parent;
  const HeaderAncestryView& view;
  const int64_t current_time;
  const UnspentTransactionsView& unspent;
};

struct HeaderValidationContext {
  const protocol::BlockHeader& header;
  const protocol::BlockHeader& parent;
  const HeaderAncestryView& view;
  const int64_t current_time;
  const int height;
};

inline HeaderValidationContext MakeHeaderContext(const BlockValidationContext& rhs) {
  return {rhs.block.Header(), rhs.parent, rhs.view, rhs.current_time, rhs.view.Length()};
}

struct BlockEnvironmentContext {
  const protocol::Block& block;
  const HeaderAncestryView& view;
  const int height;

  operator const protocol::Block&() const { return block; }
};

inline BlockEnvironmentContext MakeEnvironmentContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.view, rhs.view.Length()};
}

struct BlockSpendingContext {
  const protocol::Block& block;
  const UnspentTransactionsView& unspent;
  const int height;
};

inline BlockSpendingContext MakeBlockSpendingContext(const BlockValidationContext& rhs) {
  return {rhs.block, rhs.unspent, rhs.view.Length()};
}

namespace rules {

[[nodiscard]] Result ValidateHeader(const HeaderValidationContext& context);

[[nodiscard]] Result ValidateTransaction(
    const protocol::TransactionConstView transaction);

// Performs non-contextual block validation, aligned with Core's CheckBlock function.
[[nodiscard]] Result ValidateStructural(
    const protocol::Block& block);

[[nodiscard]] Result ValidateContextual(const BlockEnvironmentContext& context);

[[nodiscard]] Result ValidateNonSpending(const BlockEnvironmentContext& context);

[[nodiscard]] Result ValidateInputSpend(
    const protocol::TransactionConstView tx, const int input_index, const UnspentDetail& prevout,
    const int height);

[[nodiscard]] inline Result ValidateSpending(const BlockSpendingContext& context);

}  // namespace rules
}  // namespace hornet::consensus
