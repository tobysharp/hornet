#pragma once

#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"

namespace hornet::data::utxo {

class ShardTail {
 public:
  ShardTail();
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  void AppendBlock(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height);
  void UndoSince(int height);

 protected:
  static constexpr int kMaxBlocksPerRun = 8;

  struct Run {
    util::SubArray range;
    int begin_height;
    int num_blocks;
    size_t num_entries[kMaxBlocksPerRun];
    // TODO: Bloom filter.
  };
  
  static std::vector<OutputKV> GetBlockEntries(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height);

  std::vector<OutputKV> entries_;
  std::vector<Run> runs_;
};

inline ShardTail::ShardTail() {
  // TODO: Reserve sufficient entries_ capacity.
}

inline int ShardTail::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  // Since we are inside a shard, and shards exist to give parallelism across the key space during a query,
  // we don't strictly need to execute this loop in parallel. But since the queries are independent, and our
  // parallel-for mechanism is designed for extremely low overhead and re-entrancy, it is essentially free
  // to do so, and could help to reduce latency when the inter-shard parallelism is uneven.

  return ParallelSum(SplitQuery(keys), [&](const auto& range) {
    // Search runs in reverse chronological order, so we hit the most recent first.
    return std::accumulate(runs_.rbegin(), runs_.rend(), 0, [&](int sum, const Run& run) {
      const auto entries = run.range.Span(entries_);
      const auto rids_base = rids.begin() + range.begin() - keys.begin();
      return sum + ForEachMatchInDoubleSorted(range.begin(), range.end(), entries.begin(), entries.end(), rids_base,
        [](const OutputId& rid) { return IdCodec::Length(rid) == 0},  // Only include queries that haven't already been found.
        [](const OutputKV& kv, OutputId* rid) {
          *rid = kv.rid;
          return kv.IsAdd();  // Only count `Add` records towards the total count returned.
        }
      );
    });
  });
}

inline void ShardTail::AppendBlock(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height) {
  // Returns the sorted set of all newly created outputs to add and all newly spent outputs to delete.
  const std::vector<Entry> block_entries = GetBlockEntries(block, add_outputs_kvs, height);

  // Append the block's entries to the tail.
  runs_.push_back({entries_.size(), height, 1, {block_entries.size()}});
  entries_.insert(entries_.end(), block_entries.begin(), block_entries.end());
}

inline void ShardTail::UndoSince(int height) {
  for (auto it = runs_.begin(); it != runs_.end(); ) {
    auto& run = *it;
    if (run.begin_height >= height) {
      // Remove the run completely
      it = runs_.erase(it);
    } else if (run.begin_height + run.num_blocks >= height) {
      // Truncate the run
      int entries_to_remove = 0;
      for (int i = run.num_blocks - 1; run.begin_height + i >= height && i >= 0; --i)
        entries_to_remove += run.num_entries[i];
      run.range.Resize(run.range.Count() - entries_to_remove);
      run.num_blocks = height - run.begin_height - 1;
      for (int i = run.num_blocks; i < kMaxBlocksPerRun; ++i)
        run.num_entries[i] = 0;
    }
  }
  std::erase_if(entries_, [](const OuptutKV& kv) { return kv.height >= height; });
}

}  // namespace hornet::data::utxo
