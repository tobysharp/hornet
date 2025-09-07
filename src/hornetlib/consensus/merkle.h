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
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/txid.h"
#include "hornetlib/util/throw.h"

namespace hornet::consensus {

struct MerkleRoot {
  protocol::Hash hash;
  bool unique;          // True if all the sibling pairs in the Merkle binary tree are unique.
};

inline MerkleRoot ComputeMerkleRoot(const protocol::Block& block) {
  const int count = block.GetTransactionCount();
  if (count == 0) return {};

  // Allocate space for all the leaves of the tree (must be even).
  int padded = (count + 1) & ~1;  // rounds up to an even integer.
  std::vector<protocol::Hash> layer(padded);

  // Computes txids into the layer buffer
  for (int i = 0; i < count; ++i)
    layer[i] = block.Transaction(i).GetHash();

  // Hash up the tree, overwriting in place
  MerkleRoot result = { {}, true };
  for (int n = count; n > 1; n = (n + 1) >> 1) {
    // Consensus requires that none of the sibling pairs are identical twins.
    for (int i = 0; i < n; i += 2)
      if (layer[i] == layer[i + 1]) result.unique = false;
    
    // Duplicate the last item if we have an odd number of nodes at this layer.
    if (n & 1)
      layer[n] = layer[n - 1];

    // Hash the 64-byte pairs in batches across the whole layer, overwriting in place.
    crypto::DoubleSha256Batch(layer[0].begin(), 64, 64, (n + 1) >> 1, layer[0].begin(), 32);
  }

  // Return the root hash.
  result.hash = layer[0];
  return result;
}

}  // namespace hornet::consensus
