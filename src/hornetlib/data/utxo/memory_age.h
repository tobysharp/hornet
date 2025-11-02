#pragma once

#include <atomic>
#include <memory>

#include "hornetlib/data/utxo/memory_run.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/single_writer.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/util/log.h"

namespace hornet::data::utxo {

class MemoryAge {
 public:
  using MemoryRunPtr = std::shared_ptr<const MemoryRun>;
  using EnqueueFn = std::function<void(MemoryAge*)>;

  MemoryAge(bool is_mutable, int merge_fan_in = 8, EnqueueFn enqueue = {}) : is_mutable_(is_mutable), merge_fan_in_(merge_fan_in), enqueue_(std::move(enqueue)) {}

  bool IsMutable() const { return is_mutable_; }
  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const;
  int Size() const { return std::ssize(*runs_); }
  bool Empty() const { return runs_->empty(); }
  bool IsMergeReady() const;
  TiledVector<OutputKV> MakeEntries() const { return {kTileBits}; }
  void Append(MemoryRun&& run);
  void Append(TiledVector<OutputKV>&& entries, const std::pair<int, int>& range);
  void Merge(MemoryAge* dst);
  void EraseSince(int height);
  void RetainSince(int height);
  
  MemoryRunPtr RunSnapshot(int index) const { return (*runs_)[index]; }

 protected:
  static constexpr int kTileBits = 13;

  const bool is_mutable_ = false;
  const int merge_fan_in_ = 8;
  const EnqueueFn enqueue_;
  int merged_to_ = 0;
  std::atomic_bool is_merging_ = false;
  std::atomic<int> retain_height_ = std::numeric_limits<int>::max();
  SingleWriter<std::vector<MemoryRunPtr>> runs_;
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
  const auto copy = runs_.Copy();
  std::sort(copy->begin(), copy->end(), [](const MemoryRunPtr& lhs, const MemoryRunPtr& rhs) {
    return lhs->HeightRange().first < rhs->HeightRange().first;
  });
  int ready = 0;
  int height_to = merged_to_;
  for (int i = 0; i < std::min<int>(merge_fan_in_, std::ssize(*copy)); ++i) {
    if (height_to != (*copy)[i]->HeightRange().first)
      return false;  // Non contiguous ranges don't merge.
    height_to = (*copy)[i]->HeightRange().second;
    if (height_to > retain_height_)
      return false;  // Must keep in this age for now.
    ++ready;
  }
  return ready >= merge_fan_in_;
}

inline void MemoryAge::RetainSince(int height) { 
  retain_height_ = height; 
  if (enqueue_ && IsMergeReady()) enqueue_(this); 
}

inline void MemoryAge::Append(TiledVector<OutputKV>&& entries, const std::pair<int, int>& range) {
  Append(MemoryRun{is_mutable_, std::move(entries), range});
}

inline void MemoryAge::Append(MemoryRun&& run) {
  LogInfo("Appending run #", runs_->size(), " with ", run.Size(), " entries, heights [", run.HeightRange().first, ", ", run.HeightRange().second, ").");
  const auto ptr = std::make_shared<MemoryRun>(std::move(run));
  runs_.Edit()->push_back(ptr);  // Publishes the new run set immediately.
  if (enqueue_ && IsMergeReady()) enqueue_(this);
}

inline void MemoryAge::Merge(MemoryAge* dst) {
  if (!IsMergeReady()) return;  // Nothing to do.

  bool expected = false;
  if (!is_merging_.compare_exchange_strong(expected, true))
    return;
  {
    struct Guard { MemoryAge* a; ~Guard() { a->is_merging_ = false; } } guard{this};

    // NOTE: Here we take a copy-on-write lock, and hold it until the end of this function.
    // In age 0, that excludes Append from completing concurrently. That's not terrible, because
    // age 0 is small, with at most ~3 MiB data for this merge, which only runs once per merge_fan_in
    // Append calls. But later, if we see in profiling that the lock contention is non-zero, we can
    // improve the situation by taking a Snapshot here and only calling Edit when we do the final erase.
    // That in turn requires us to guarantee that EraseSince cannot be called concurrently, which
    // means we would have to pause the compacter thread when the shard's EraseSince method is called.
    // Since that's more complexity for a probably marginal return, we'll wait until profiling indicates
    // that it's worth the effort to implement. Meanwhile, everything here is safe, and the only 
    // slightly sub-optimal contention is during Append vs Merge in Age 0.
    auto copy = runs_.Edit();
    std::sort(copy->begin(), copy->end(), [](const MemoryRunPtr& lhs, const MemoryRunPtr& rhs) {
      return lhs->HeightRange().first < rhs->HeightRange().first;
    });
    if (std::ssize(*copy) < merge_fan_in_ || (*copy)[merge_fan_in_ - 1]->HeightRange().second > retain_height_)
      return;
    const auto inputs = std::span{*copy}.first(merge_fan_in_);
    const int end_merge_height = inputs.back()->HeightRange().second;
    LogInfo("Merging upward heights [", inputs.front()->HeightRange().first, ", ", inputs.back()->HeightRange().second,
            "), remaining ", copy->size() - inputs.size(), " runs.");
    dst->Append(MemoryRun::Merge(dst->is_mutable_, inputs));
    copy->erase(copy->begin(), copy->begin() + merge_fan_in_);
    merged_to_ = end_merge_height;
  }

  // Requeue if more merges are possible.
  if (enqueue_ && IsMergeReady()) enqueue_(this);
}

inline void MemoryAge::EraseSince(int height) {
  Assert(IsMutable());

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
