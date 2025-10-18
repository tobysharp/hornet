#pragma once

#include <compare>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

/*
  Notes:

  The index contains two parts: a permanent key-value store, and an in-memory mutable tail.

  Goals and Properties:

  - Separate QueryTail and QueryMain. Calling the two in sequence should ignore any duplicates
  found in both places, and enable caller to determine whether every query was found.
  
  - AppendTail appends elements to the in-memory tail and then atomically updates the tail size.
  We pre-allocate sufficient space for the tail to grow so this won't cause a re-allocation.
  
  - CommitBefore is run in a background thread. Some elements from the tail are copied for 
  graduation to the main index store. Periodically, a new main index shard is copy-merged with the
  tail elements, then atomically published to replace the old shard. Afterwards, the graduating
  items are removed from the tail under an exclusive lock.

  - RemoveSince truncates the tail size atomically to delete the end of the tail.

*/
class Index {
 public:
  Index(const std::filesystem::path& folder, int shard_bits = 9, int dictionary_bits = 7) {
    shards_.reserve(1 << shard_bits);
    for (int i = 0; i < 1 << shard_bits; ++i) shards_.emplace_back(shard_bits, dictionary_bits);
  }

  int ShardCount() const {
    return std::ssize(shards_);
  }

  static void SortKeys(std::span<OutputKey> prevouts);

  int QueryTail(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  int QueryMain(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  //void SortMergeToTail(std::span<OutputKV> pairs, int height);
  void AddAndDeleteOutputs(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height);
  void RemoveSince(int height);

 protected:
  struct TailEntry {
    OutputKV kv;
    int height;
  };
  static void SortKeys(std::span<OutputKV> kvs);

  std::vector<Shard> shards_;   // The shards of the main index.
  std::vector<TailEntry> tail_; // The in-memory sorted tail.
  std::atomic<int> tail_size_;  // The atomic size of the tail.
  std::mutex tail_mutex_;
};

/* static */ inline void Index::SortKeys(std::span<OuptutKV> pairs) {
}

inline int Index::QueryTail(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const {
  Assert(prevouts.size() == ids.size());
  constexpr int kSlicesCount = 16;

  // TODO: Parallelize over subranges of the query array.
  // const auto ranges = Split(prevouts, kSlicesCount);
  const auto ranges = std::array{ prevouts };
  std::vector<int> counts(ranges.size());
  std::scoped_lock lock(tail_mutex_);

  ParallelFor(0, std::ssize(ranges), [&](int i) {
    const auto& range = ranges[i];
    counts[i] = ForEachMatchInDoubleSorted(range.begin(), range.end(), tail_.begin(), tail_.end(),
      [](const TailEntry& entry) { return entry.kv; }, std::compare_three_way{}, 
      [&](int, int query_index, const OutputKV& entry) {
        const int global_index = query_index + (range.begin() - prevouts.begin());
        ids[global_index] = entry.value;
      });
  });
  return std::accumulate(counts.begin(), counts.end(), 0);
}

inline int Index::QueryMain(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const {
  Assert(prevouts.size() == ids.size());

  // Parallelize over shards of the index, skipping queries that don't pertain to that shard.
  std::vector<int> count(shards_.size(), 0);
  ParallelFor(0, std::ssize(shards_), [&](int i) {
    count[i] = shards_[i].Query(prevouts, ids);
  });
  return std::accumulate(count.begin(), count.end(), 0);
}

inline void Index::AddAndDeleteOutputs(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height) {

}

inline void Index::SortMergeToTail(std::span<OutputKV> pairs, int height) {
  // Since the tail is kept sorted by key, we can't simply append to the end of the
  // tail here. Instead we have to merge the old tail with the new entries. This can
  // be done with a parallel merge sort. As for concurrency, a minimal model could use
  // a double-buffered tail (entries and size), prepare the merge in the inactive tail,
  // and then atomically publish the pointer to the new tail. That would allow all tail
  // readers to run concurrently. However, when we consider that the only tail reader is
  // QueryTail, and that doesn't need to run concurrently with AppendTail as they are
  // strictly sequential operations serialized by design, then it's actually simpler
  // to notice that we can just hold a simple lock here to exclude the background 
  // compacter thread from overlapping with this operation.

  SortKeys(pairs);

  std::scoped_lock lock(tail_mutex_);

  const auto mid = tail_.end();
  tail_.insert(mid, pairs.begin(), pairs.end());
  // TODO: Convert this serial merge to a parallel merge.
  std::inplace_merge(tail_.begin(), mid, tail_.end());
}

inline void Index::RemoveSince(int height) {
  std::scoped_lock lock(tail_mutex_);
  std::erase_if(tail_, [&](const TailEntry& entry) { return entry.height >= height; });
}

}  // namespace hornet::data::utxo

