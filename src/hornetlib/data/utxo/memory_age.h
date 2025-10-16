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

  MemoryAge(bool is_mutable = false) : is_mutable_(is_mutable), runs_(std::make_shared<MemoryRunSet>()) {
  }
  bool IsMutable() const { 
    return is_mutable_; 
  }
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  
  void Append(MemoryRunPtr&& run);
  
 protected:
  const bool is_mutable_;
  std::atomic<MemoryRunSetPtr> runs_;
};

inline int MemoryAge::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  const MemoryRunSetPtr snapshot = runs_;
  return std::accumulate(snapshot->rbegin(), snapshot->rend(), 0, [&](int sum, const MemoryRunPtr& run) {
    if (sum == std::ssize(keys)) return sum;
    return sum + run->Query(keys, rids);
  });
}

inline void MemoryAge::Append(MemoryRun&& run) {
  const MemoryRunSetPtr snapshot = runs_;
  auto copy = std::make_shared<MemoryRunSet>(*snapshot);
  copy->emplace_back(std::make_shared<MemoryRun>(std::move(run)));
  runs_ = copy;
}

}  // namespace hornet::data::utxo
