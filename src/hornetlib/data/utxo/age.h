#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "hornetlib/data/utxo/directory.h"
#include "hornetlib/data/utxo/search.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/thread_safe_queue.h"

namespace hornet::data::utxo {

using Directory = std::vector<std::span<const OutputKV>>;

struct MemoryRun {
  static constexpr int kMaxUndoHeights = 12;
  TiledVector<OutputKV> entries;
  Directory directory;
  // TODO: Bloom filter
  std::pair<int, int> height_range;
  std::array<size_t, kMaxUndoHeights> entries_per_height = {};
};

using RunPtr = std::shared_ptr<const MemoryRun>;
using RunList = std::vector<RunPtr>;
using RunListPtr = std::shared_ptr<const RunList>;

class MemoryAge {
 public:
  MemoryAge(int merge_fan_in) : merge_fan_in_(merge_fan_in). runs_(std::make_shared<RunList>()) {}
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  void AppendRun(RunPtr run);

  bool IsCompactReady() const { return std::ssize(*runs_) >= merge_fan_in_; }
  RunList PeekRunsToCompact() const;
  void EatCompactedRuns(RunList eat);

 protected:
  std::vector<std::span<const OutputKey>> SplitQuery(std::span<const OutputKey> keys) const;

  const int merge_fan_in_;
  std::atomic<RunListPtr> runs_;
  std::mutex mutex_;  // Serializes mutations of runs_.
};

inline int MemoryAge::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  // Since we are inside a shard, and shards exist to give parallelism across the key space during a query,
  // we don't strictly need to execute this loop in parallel. But since the queries are independent, and our
  // parallel-for mechanism is designed for extremely low overhead and re-entrancy, it is essentially free
  // to do so, and could help to reduce latency when the inter-shard parallelism is uneven.
  Assert(keys.size() == rids.size());
  if (keys.empty()) return 0;

  const RunListPtr snapshot = std::atomic_load_explicit(&runs_, std::memory_order_acquire);
  return ParallelSum(SplitQuery(keys), [&](const auto& range) {
    // Search runs in reverse chronological order, so we hit the most recent first.
    return std::accumulate(snapshot->rbegin(), snapshot->rend(), 0, [&](int sum, const RunPtr& run) {
      const auto entries = run->directory.Lookup(keys[0]);
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

class MutableMemoryAge : public MemoryAge {
 public:
  MutableMemoryAge(int merge_fan_in = 8) : MemoryAge(merge_fan_in) {}
  void UndoSince(int height);
};

class ShardTail : public MutableMemoryAge {
 public:
  void AppendBlock(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height);
};

struct MergeJob {
  RunList inputs;
  MemoryAge* src;
  MemoryAge* dst;
};

class Compactor {
 public:
  void Enqueue(MergeJob&& job) { jobs_.push(std::move(job)); }
  void Loop();
 private:
  util::ThreadSafeQueue<MergeJob> jobs_;
};



inline void MutableMemoryAge::UndoSince(int height) {
  std::scoped_lock lock(mutex_);

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
