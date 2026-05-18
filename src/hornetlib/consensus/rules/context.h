#pragma once

#include <cstdint>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/scripts/patterns.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"

namespace hornet::consensus::rules {

struct TransactionsInBlock {
  auto operator()(const protocol::Block& block) const { 
    return block.Transactions();
  }
};

struct BlockValidationContext {
  const protocol::Block& block;
  const protocol::BlockHeader& parent;
  const HeaderAncestryView& view;
  const int64_t current_time;
  const UnspentOutputsView& unspent;
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

struct WitnessContext {
  const protocol::Block& block;
  const std::optional<std::span<const uint8_t>> commitment;
};

[[nodiscard]] inline WitnessContext MakeWitnessContext(const BlockEnvironmentContext& context) {
  Assert(IsBIPActiveAtHeight(BIP::SegWit, context.height));
  return {context.block, scripts::ExtractWitnessCommitment(context.block)};  
}

struct BlockSpendContext {
  const protocol::Block& block;
  const HeaderAncestryView& ancestry;
  const UnspentOutputsView& unspent;
  const int height;
  const uint64_t script_flags;
};

struct SpendsInBlock {
  auto operator()(const BlockSpendContext& context) const {
    const auto spends = context.unspent.Spends(context.block);  
    return spends ? *spends : JoinedSpendRange{};
  }
};

struct TransactionSpendContext {
  const protocol::TransactionConstView tx;
  const std::span<const SpendRecord> spends;
  const HeaderAncestryView& ancestry;
  const int height;
};

struct InputsInSpend {
  auto operator()(const TransactionSpendContext& context) const {
    return context.spends;
  }
};

struct MakeTransactionSpendContext {
  TransactionSpendContext operator()(const JoinedSpend& tx_spends, const BlockSpendContext& context) const {
    return {tx_spends.tx, tx_spends.spends, context.ancestry, context.height};
  }
};

struct InputSpendContext {
  const protocol::TransactionConstView tx;
  const SpendRecord spend;
  const int height;
};

struct MakeInputSpendContext {
  InputSpendContext operator()(const SpendRecord& spend, const TransactionSpendContext& context) const {
    return {context.tx, spend, context.height};
  }
};

}  // namespace hornet::consensus::rules
