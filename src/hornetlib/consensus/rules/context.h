#pragma once

#include <cstdint>

#include "hornetlib/consensus/header_ancestry_view.h"
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

}  // namespace hornet::consensus::rules
