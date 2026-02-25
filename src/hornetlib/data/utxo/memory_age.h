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
  void PinHeight(int height);
  void UnpinHeight(int height);
  TiledVector<OutputKV> MakeEntries() const { return {kTileBits}; }
  void Append(MemoryRun&& run);
  void Append(TiledVector<OutputKV>&& entries, const std::pair<int, int>& range);
  void Merge(MemoryAge* dst);
  void EraseSince(int height);
  bool ContainsHeight(int height) const;

  auto RunsSnapshot() const { return runs_.Snapshot(); }

 protected:
  using MemoryRunPtr = AtomicVector<MemoryRun>::Ptr;
  static constexpr int kTileBits = 13;

  const bool is_mutable_ = false;
  const int merge_fan_in_ = 8;
  const EnqueueFn enqueue_;
  std::atomic<int> merged_to_ = 0;
  std::atomic<bool> is_merging_ = false;
  AtomicVector<MemoryRun> runs_;
  AtomicVector<int> pins_;
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
  const int start_height = merged_to_;

  // Check for a sufficiently long range of contiguous heights. 
  int ready = 0;
  int height_to = start_height;
  for (int i = 0; i < std::min<int>(merge_fan_in_, std::ssize(*copy)); ++i) {
    if (height_to != (*copy)[i]->HeightRange().first)
      return false;  // Non contiguous ranges don't merge.
    height_to = (*copy)[i]->HeightRange().second;
    ++ready;
  }
  if (ready < merge_fan_in_) return false;

  // Check for any pinned heights in the merge range [start_height, height_to).
  if (const auto pins = pins_.Snapshot(); !pins->empty()) {
    const auto it = std::lower_bound(pins->begin(), pins->end(), start_height, [](const auto& ptr, int value) {
      return *ptr < value;
    });
    if (it != pins->end() && **it < height_to) return false;
  }

  return true;
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
  bool expected = false;
  if (!is_merging_.compare_exchange_strong(expected, true))
    return;

  struct Guard { MemoryAge* a; ~Guard() { a->is_merging_ = false; } } guard{this};

  if (!IsMergeReady()) return;  // Nothing to do.

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
  const int start_height = merged_to_;
  const auto inputs = std::span{*copy}.first(merge_fan_in_);

  Assert(std::ssize(inputs) == merge_fan_in_);
  Assert(inputs.front()->HeightRange().first == start_height);
  Assert(start_height % merge_fan_in_ == 0);
  
  int h = start_height;
  for (const auto& r : inputs) {
    const auto [begin, end] = r->HeightRange();
    Assert(begin == h);
    h = end;
  }

#if UTXO_LOG
  LogDebug("Merging upward heights [", inputs.front()->HeightRange().first, ", ", inputs.back()->HeightRange().second,
          "), remaining ", copy->size() - inputs.size(), " runs.");
#endif
  dst->Append(MemoryRun::Merge(dst->is_mutable_, inputs));
  runs_.EraseFront(merge_fan_in_);
  merged_to_ = inputs.back()->HeightRange().second;

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

inline bool MemoryAge::ContainsHeight(int height) const {
  for (const auto& run : *runs_.Snapshot()) {
    const auto range = run->HeightRange();
    if (range.first > height) return false;
    if (height < range.second) return true;
  }
  return false;
}

inline void MemoryAge::PinHeight(int height) {
  pins_.InsertOrAssign(height, std::less{});
}

inline void MemoryAge::UnpinHeight(int height) {
  pins_.EraseIf([=](int cmp) { return cmp == height; });
  if (enqueue_ && IsMergeReady()) enqueue_(this);
}

}  // namespace hornet::data::utxo
