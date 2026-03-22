#pragma once

#include <cstdint>

#include "hornetlib/consensus/bips.h"
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

struct WitnessContext {
  const protocol::Block& block;
  const int commit_index = -1;
};

[[nodiscard]] inline WitnessContext MakeWitnessContext(const BlockEnvironmentContext& context) {
  Assert(IsBIPActiveAtHeight(BIP::SegWit, context.height));
  using protocol::script::lang::Op;
  constexpr std::array<uint8_t, 6> kCommitmentPrefix = {+Op::Return, 0x24, 0xaa, 0x21, 0xa9, 0xed};
  const protocol::Block& block = context.block;

  const int output_index = [&] {
    if (!block.Empty()) {
      for (int i = block.Transaction(0).OutputCount() - 1; i >= 0; --i)
        if (std::ranges::starts_with(block.Transaction(0).PkScript(i), kCommitmentPrefix)) return i;
    }
    return -1;
  }();
  return {block, output_index};
}

}  // namespace hornet::consensus::rules
