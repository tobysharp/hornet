// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <vector>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/protocol/txid.h"
#include "hornetlib/util/throw.h"

namespace hornet::consensus {

// Represents the Merkle root of a binary tree, along with a flag for uniqueness.
struct MerkleRoot {
  protocol::Hash hash;
  bool unique;          // True if all the sibling pairs in the Merkle binary tree are unique.
};

// Helper class to build a Merkle tree in place.
class MerkleReducer {
 public:
  MerkleReducer(int count) : count_(count), nodes_((count + 1) & ~1) {}
  protocol::Hash& operator[](int index) { return nodes_[index]; }

  inline MerkleRoot Compute() {
    if (count_ == 0) return {{}, true};

    // Hash up the tree, overwriting in place
    MerkleRoot result = { {}, true };
    for (int n = count_; n > 1; n = (n + 1) >> 1) {
      // Consensus requires that none of the sibling pairs are identical twins.
      for (int i = 0; i < n; i += 2)
        if (nodes_[i] == nodes_[i + 1]) result.unique = false;
      
      // Duplicate the last item if we have an odd number of nodes at this layer.
      if (n & 1)
        nodes_[n] = nodes_[n - 1];

      // Hash the 64-byte pairs in batches across the whole layer, overwriting in place.
      crypto::DoubleSha256Batch(nodes_[0].begin(), 64, 64, (n + 1) >> 1, nodes_[0].begin(), 32);
    }

    // Return the root hash.
    result.hash = nodes_[0];
    return result;
  }
  
 private:
  int count_;
  std::vector<protocol::Hash> nodes_;
};

// Computes the Merkle root of a set of leaves, given a function to obtain each leaf by index.
template <typename Func>
inline MerkleRoot ComputeMerkleRoot(int count, Func leaf_func) {
  MerkleReducer builder(count);
  for (int i = 0; i < count; ++i)
    builder[i] = leaf_func(i);
  return builder.Compute();
}

// Computes the legacy Merkle root (txid-based) of a block.
inline MerkleRoot ComputeMerkleRoot(const protocol::Block& block) {
  return ComputeMerkleRoot(block.GetTransactionCount(), [&](int i) {
      return block.Transaction(i).GetHash();
  });
}

// Computes the witness Merkle root (wtxid-based) of a block.
inline MerkleRoot ComputeWitnessMerkleRoot(const protocol::Block& block) {
  return ComputeMerkleRoot(block.GetTransactionCount(), [&](int i) {
      return i == 0 ? protocol::Hash{} : block.Transaction(i).GetWitnessHash();
  });
}

}  // namespace hornet::consensus
