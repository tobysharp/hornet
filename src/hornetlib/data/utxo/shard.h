#pragma once

#include <span>
#include <memory>
#include <vector>

#include "hornetlib/data/utxo/compacter.h"
#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

// A shard of the unspent outputs index.
class Shard {
 public:
  Shard(const std::vector<MemoryAge::Options>& age_options);

  // Called by the index with each Append according to its mutability window.
  void SetMutableSince(int height) {
    // Set the merge policy for the last mutable age before immutability occurs.
    for (auto it = ages_.rbegin(); it != ages_.rend(); ++it)
      if ((*it)->IsMutable()) (*it)->RetainSince(height);
  }

  //int QueryRecent(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  //int QueryMain(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  //int QueryAll(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  int Query(std::span<const OutputKey> prevouts, std::span<OutputId> ids, int height) const;
  // TODO: Other methods Append, EraseSince, etc.

  // Permit entries from the given block height to be merged from the recent index into the main index.
  //void PermitMain(int height) { ages_[0]->RetainSince(height + 1); }

 private:
  struct QueryRange {
    std::span<const OutputKey> keys;
    std::span<OutputId> ids;
  };
  static std::vector<QueryRange> SplitQuery(const QueryRange& full, int splits) const;

  Compacter compacter_;
  std::vector<std::unique_ptr<MemoryAge>> ages_;
};

inline Shard::Shard(const std::vector<MemoryAge::Options>& age_options) {
  ages_.resize(age_options.size());
  ages_.back() = std::make_unique<MemoryAge>(age_options.back());
  for (int i = std::ssize(ages_) - 2; i >= 0; ++i) {
    MemoryAge* parent = ages_[i + 1].get();
    ages_[i] = std::make_unique<MemoryAge>(age_options[i], 
      [&, parent](MemoryAge* src) { compacter_.EnqueueMerge(src, parent); });
  }
}

int Shard::Query(std::span<const OutputKey> keys, std::span<OutputId> ids, int height) const {
  // Since we are inside a shard, and shards exist to give parallelism across the key space during a query,
  // we don't strictly need to execute this loop in parallel. But since the queries are independent, and our
  // parallel-for mechanism is designed for extremely low overhead and re-entrancy, it is essentially free
  // to do so, and could help to reduce latency when the inter-shard parallelism is uneven.  
  static constexpr int kRanges = 8;
  return ParallelSum(SplitQuery({keys, ids}, kRanges), [&](const auto& range) {
    return std::accumulate(ages_.begin(), ages_.end(), 0, [&](int sum, const auto& age) {
      // Note: If the queried age is immutable, it will throw an exception if height is within its data range.
      return sum + age->Query(range.keys, range.ids, height);
    });
  });
}

}  // namespace hornet::data::utxo
