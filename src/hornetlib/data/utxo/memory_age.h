#pragma once

#include <atomic>
#include <memory>

#include "hornetlib/data/utxo/atomic_vector.h"
#include "hornetlib/data/utxo/memory_run.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/util/log.h"

namespace hornet::data::utxo {

class MemoryAge {
 public:
  using EnqueueFn = std::function<void(MemoryAge*)>;

  MemoryAge(bool is_mutable, int merge_fan_in = 8, EnqueueFn enqueue = {}) : is_mutable_(is_mutable), merge_fan_in_(merge_fan_in), enqueue_(std::move(enqueue)) {}

  bool IsMutable() const { return is_mutable_; }
  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const;
  int Size() const { return runs_.Size(); }
  bool Empty() const { return runs_.Empty(); }
  bool IsMergeReady() const;
  TiledVector<OutputKV> MakeEntries() const { return {kTileBits}; }
  void Append(MemoryRun&& run);
  void Append(TiledVector<OutputKV>&& entries, const std::pair<int, int>& range);
  void Merge(MemoryAge* dst);
  void EraseSince(int height);
  
  auto RunSnapshot(int index) const { return runs_[index]; }

 protected:
  using MemoryRunPtr = AtomicVector<MemoryRun>::Ptr;
  static constexpr int kTileBits = 13;

  const bool is_mutable_ = false;
  const int merge_fan_in_ = 8;
  const EnqueueFn enqueue_;
  std::atomic<int> merged_to_ = 0;
  std::atomic<bool> is_merging_ = false;
  AtomicVector<MemoryRun> runs_;
};

inline QueryResult MemoryAge::Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const {
  const auto snapshot = runs_.Snapshot();
  return std::accumulate(snapshot->rbegin(), snapshot->rend(), QueryResult{},
    [&](const QueryResult& sum, const MemoryRunPtr& run) {
      if (sum.funded + sum.spent == std::ssize(keys)) return sum;
      return sum + run->Query(keys, rids, since, before);
    }
  );
}

inline bool MemoryAge::IsMergeReady() const {
  const auto copy = runs_.Snapshot();
  Assert(std::is_sorted(copy->begin(), copy->end(), [](const MemoryRunPtr& lhs, const MemoryRunPtr& rhs) {
    return lhs->HeightRange().first < rhs->HeightRange().first;
  }));
  int ready = 0;
  int height_to = merged_to_;
  for (int i = 0; i < std::min<int>(merge_fan_in_, std::ssize(*copy)); ++i) {
    if (height_to != (*copy)[i]->HeightRange().first)
      return false;  // Non contiguous ranges don't merge.
    height_to = (*copy)[i]->HeightRange().second;
    ++ready;
  }
  return ready >= merge_fan_in_;
}

inline void MemoryAge::Append(TiledVector<OutputKV>&& entries, const std::pair<int, int>& range) {
  Append(MemoryRun{is_mutable_, std::move(entries), range});
}

inline void MemoryAge::Append(MemoryRun&& run) {
#if UTXO_LOG
  LogDebug("Appending run #", runs_.Size(), " with ", run.Size(), " entries, heights [", run.HeightRange().first, ", ", run.HeightRange().second, ").");
#endif
  runs_.Insert(std::move(run), [](const auto& lhs, const auto& rhs) {
    return lhs.HeightRange().first < rhs.HeightRange().first;
  });
  if (enqueue_ && IsMergeReady()) enqueue_(this);
}

inline void MemoryAge::Merge(MemoryAge* dst) {
  if (!IsMergeReady()) return;  // Nothing to do.

  bool expected = false;
  if (!is_merging_.compare_exchange_strong(expected, true))
    return;
  {
    struct Guard { MemoryAge* a; ~Guard() { a->is_merging_ = false; } } guard{this};

    // NOTE: Here we could take a copy-on-write lock, and hold it until the end of this function.
    // However, observe that we only perform the merge and publish a change to runs_ if we have enough
    // contiguous data at the front of the runs_ vector. Holes are not permitted in merged runs.
    // Any concurrent appends will therefore necessarily be inserted *after* the data being merged.
    // Therefore we don't have a race for the elements being merged at the head of runs_. Consequently,
    // we don't need to hold the lock during the merge, and can just erase the correct number of items
    // from the front of runs_ after the merge. Since Merge operations are serialized by the is_merging_
    // variable, and cannot overlap with EraseSince because of explicit Pause/Resume in the Compacter,
    // there are no other writers to consider.
    const auto copy = runs_.Snapshot();
    if (std::ssize(*copy) < merge_fan_in_) return;
    Assert(std::is_sorted(copy->begin(), copy->end(), [](const MemoryRunPtr& lhs, const MemoryRunPtr& rhs) {
      return lhs->HeightRange().first < rhs->HeightRange().first;
    }));
    const auto inputs = std::span{*copy}.first(merge_fan_in_);
    const int end_merge_height = inputs.back()->HeightRange().second;
#if UTXO_LOG
    LogDebug("Merging upward heights [", inputs.front()->HeightRange().first, ", ", inputs.back()->HeightRange().second,
            "), remaining ", copy->size() - inputs.size(), " runs.");
#endif
    dst->Append(MemoryRun::Merge(dst->is_mutable_, inputs));
    runs_.EraseFront(merge_fan_in_);
    merged_to_ = end_merge_height;
  }

  // Requeue if more merges are possible.
  if (enqueue_ && IsMergeReady()) enqueue_(this);
}

inline void MemoryAge::EraseSince(int height) {
  Assert(IsMutable());
  Assert(!is_merging_);

  auto copy = runs_.Edit();
  auto it = copy->begin();
  while (it != copy->end()) {
    const auto [begin, end] = (*it)->HeightRange();
    if (height <= begin) it = copy->erase(it);
    else if (height < end) {
      auto replace = std::make_shared<MemoryRun>(**it);
      replace->EraseSince(height);
      *it = std::move(replace);
    } else ++it;
  }
}

}  // namespace hornet::data::utxo
