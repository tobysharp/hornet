#pragma once

#include <atomic>
#include <memory>

#include "hornetlib/data/utxo/memory_run.h"
#include "hornetlib/data/utxo/single_writer.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class MemoryAge {
 public:
  using MemoryRunPtr = std::shared_ptr<const MemoryRun>;
  using EnqueueFn = std::function<void(MemoryAge*)>;
  struct Options {
    MemoryRun::Options run;
    int merge_fan_in = 8;  // Runs per age.
  };

  MemoryAge(Options options, EnqueueFn enqueue = {}) : options_(std::move(options)), enqueue_(std::move(enqueue)) {}

  // bool IsMutable() const { return options_.run.is_mutable; }
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  int Size() const { return std::ssize(*runs_); }
  bool IsMergeReady() const { return Size() >= options_.merge_fan_in; }

  void Append(MemoryRun&& run);
  void Merge(MemoryAge* dst);
  void EraseSince(int height);

 protected:
  const Options options_;
  const EnqueueFn enqueue_;
  SingleWriter<std::vector<MemoryRunPtr>> runs_;
};

inline int MemoryAge::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  const auto snapshot = runs_.Snapshot();
  return std::accumulate(snapshot->rbegin(), snapshot->rend(), 0,
                         [&](int sum, const MemoryRunPtr& run) {
                           if (sum == std::ssize(keys)) return sum;
                           return sum + run->Query(keys, rids);
                         });
}

inline void MemoryAge::Append(MemoryRun&& run) {
  const auto ptr = std::make_shared<MemoryRun>(std::move(run));
  runs_.Edit()->push_back(ptr);  // Publishes the new run set immediately.
  if (IsMergeReady()) enqueue_(this);
}

inline void MemoryAge::Merge(MemoryAge* dst) {
  if (!IsMergeReady()) return;  // Nothing to do.

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

  auto copy = dst->runs_.Edit();
  const auto inputs = std::span{*copy}.first(options_.merge_fan_in);
  dst->Append(MemoryRun::Merge(inputs, dst->options_.run));
  copy->erase(copy->begin(), copy->begin() + options_.merge_fan_in);
}

inline void MemoryAge::EraseSince(int height) {
  if (!IsMutable()) return;

  auto copy = runs_.Edit();
  auto it = copy->begin();
  while (it != copy->end()) {
    const auto [begin, end] = it->HeightRange();
    if (height <= begin) it = copy->erase(it);
    else if (height < end) {
      auto replace = std::make_shared<MemoryRun>(**it);
      replace->EraseSince(height);
      *it = std::move(replace);
    } else ++it;
  }
}

}  // namespace hornet::data::utxo
