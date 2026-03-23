#pragma once

#include <cstdint>

#include "hornetlib/consensus/bips.h"
#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/consensus/rules/scripts/patterns.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/block_header.h"

namespace hornet::consensus::rules {

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

}  // namespace hornet::consensus::rules
