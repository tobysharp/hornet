// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <tuple>
#include <vector>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/throw.h"

namespace hornet::consensus {

inline protocol::Hash ComputeMerkleRoot(const protocol::Block& block) {
  const int count = block.GetTransactionCount();
  if (count == 0)
    util::ThrowInvalidArgument("Block has no transactions.");
  int padded = (count + 1) & ~1;  // rounds up to an even integer.
  std::vector<protocol::Hash> layer(padded);

  // Computes txids into the layer buffer
  for (int i = 0; i < count; ++i)
    layer[i] = block.Transaction(i).GetHash();

  // Hash up the tree, overwriting in place
  for (int n = count; n > 1; n = (n + 1) >> 1) {
    if (n & 1)
      layer[n] = layer[n - 1];
    crypto::DoubleSha256Batch(layer[0].begin(), 64, 64, (n + 1) >> 1, layer[0].begin(), 32);
  }
  return layer[0];
}

}  // namespace hornet::consensus
