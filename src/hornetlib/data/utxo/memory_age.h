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

  MemoryAge(int merge_fan_in, bool is_mutable = false);

  bool IsMutable() const { return is_mutable_; }
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  int Size() const { return std::ssize(*runs_); }
  bool IsMergeReady() const { return Size() >= merge_fan_in_; }

  void Append(MemoryRun&& run);
  void Merge(MemoryAge* dst);

 protected:
  const int merge_fan_in_;
  const bool is_mutable_;
  SingleWriter<std::vector<MemoryRunPtr>> runs_;
};

inline MemoryAge::MemoryAge(int merge_fan_in, bool is_mutable = false)
    : merge_fan_in_(merge_fan_in), is_mutable_(is_mutable) {}

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
  runs_.CopyOnWrite()->push_back(ptr);
}

inline void MemoryAge::Merge(MemoryAge* dst) {
  if (runs_.Snapshot()->size() < merge_fan_in_) return;  // Nothing to do.
  const auto copy = dst->runs_.CopyOnWrite();
  const auto inputs = std::span{*copy}.first(merge_fan_in_);
  dst->Append(MemoryRun::Merge(inputs, dst->IsMutable()));
  copy->erase(copy->begin(), copy->begin() + merge_fan_in_);
}

}  // namespace hornet::data::utxo
