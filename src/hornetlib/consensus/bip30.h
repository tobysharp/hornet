#pragma once

#include <array>

#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::consensus {

// Table 2: the two blocks exempt from BIP30. Each repeats an earlier coinbase while that coinbase's output is still
// unspent, so each overwrites an existing output rather than creating a new one.
struct BIP30ExceptionBlock {
  int height;
  protocol::Hash hash;
};

inline constexpr std::array<BIP30ExceptionBlock, 2> kBIP30ExceptionBlocks = {{
    {91842, "00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec"_sha256},
    {91880, "00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721"_sha256},
}};

// Returns true iff the header at the given height is one of the blocks listed in Table 2.
[[nodiscard]] inline bool IsBIP30ExceptionBlock(int height, const protocol::BlockHeader& header) {
  for (const auto& block : kBIP30ExceptionBlocks)
    if (block.height == height) return header.ComputeHash() == block.hash;
  return false;
}

}  // namespace hornet::consensus
