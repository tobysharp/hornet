#pragma once

#include <atomic>
#include <memory>

#include "hornetlib/data/utxo/memory_run.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class MemoryAge {
 public:
  using MemoryRunPtr = std::shared_ptr<const MemoryRun>;
  using MemoryRunSet = std::vector<MemoryRunPtr>;
  using MemoryRunSetPtr = std::shared_ptr<const MemoryRunSet>;

  MemoryAge(int merge_fan_in, bool is_mutable = false);
  bool IsMutable() const {
    return is_mutable_;
  }
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  int Size() const {
    return std::ssize(*runs_);
  }
  bool IsMergeReady() const { return Size() >= merge_fan_in_; }

  void Append(MemoryRun&& run);

  void Merge(MemoryAge* dst);

 protected:
  MemoryRunSetPtr Snapshot() const { return runs_; }
  void Publish(MemoryRunSetPtr ptr) { runs_ = ptr; }

  const int merge_fan_in_;
  const bool is_mutable_;
  std::atomic<MemoryRunSetPtr> runs_;
};

inline MemoryAge::MemoryAge(int merge_fan_in, bool is_mutable = false)
    : merge_fan_in_(merge_fan_in),
      is_mutable_(is_mutable),
      runs_(std::make_shared<MemoryRunSet>()) {}

inline int MemoryAge::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  const MemoryRunSetPtr snapshot = Snapshot();
  return std::accumulate(snapshot->rbegin(), snapshot->rend(), 0,
                         [&](int sum, const MemoryRunPtr& run) {
                           if (sum == std::ssize(keys)) return sum;
                           return sum + run->Query(keys, rids);
                         });
}

inline void MemoryAge::Append(MemoryRun&& run) {
  const MemoryRunSetPtr snapshot = Snapshot();
  auto copy = std::make_shared<MemoryRunSet>(*snapshot);
  copy->emplace_back(std::make_shared<MemoryRun>(std::move(run)));
  Publish(copy);
}

inline void MemoryAge::Merge(MemoryAge* dst) {
  const MemoryRunSetPtr snapshot = Snapshot();
  if (snapshot->size() < merge_fan_in_) return;  // Nothing to do.

  const auto inputs = std::span{*snapshot}.first(merge_fan_in_);
  dst->Append(MemoryRun::Merge(inputs, dst->IsMutable()));
  Publish(std::make_shared<MemoryRunSet>(snapshot->begin() + merge_fan_in_, snapshot->end()));
}

}  // namespace hornet::data::utxo
