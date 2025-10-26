#pragma once

#include <memory>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/compacter.h"
#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class Index {
 public:
  Index();

  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> ids, int since, int before) const;
  TiledVector<OutputKV> MakeAppendBuffer() const { return ages_[0]->MakeEntries(); }
  void Append(TiledVector<OutputKV>&& entries, int height);
  void EraseSince(int height);

 private:
  // Called with each Append according to the mutability window.
  void SetMutableSince(int height) {
    // Set the merge policy for the last mutable age before immutability occurs.
    for (auto it = ages_.rbegin(); it != ages_.rend(); ++it)
      if ((*it)->IsMutable()) (*it)->RetainSince(height);
  }

  Compacter compacter_;
  std::vector<std::unique_ptr<MemoryAge>> ages_;

  static constexpr struct AgeOptions {
    bool is_mutable;
    int prefix_bits;
  } options_[] = {
    { true, 8 }, { true, 8 }, { true, 10}, { false, 12}, 
    { false, 13}, { false, 15}, {false, 16}, { false, 17 }
  };
  static constexpr int kAges = sizeof(options_) / sizeof(options_[0]);
};

Index::Index() : compacter_(kAges) {
  for (int i = 0; i < kAges; ++i) {
    const auto& entry = options_[i];
    ages_.emplace_back(std::make_unique<MemoryAge>(entry.is_mutable, entry.prefix_bits, [this, i](MemoryAge* src) {
      if (i < kAges - 1) compacter_.EnqueueMerge(src, ages_[i + 1].get());
    }));
  }
}

inline QueryResult Index::Query(std::span<const OutputKey> keys, std::span<OutputId> ids, int since, int before) const {
  static constexpr int kRanges = 8;
  return ParallelSum(SplitQuery({keys, ids}, kRanges), [&](const auto& range) {
    return std::accumulate(ages_.begin(), ages_.end(), QueryResult{}, [&](const QueryResult& sum, const auto& age) {
      // Note: If the queried age is immutable, it will throw an exception if height is within its data range.
      return sum + age->Query(range.keys, range.ids, since, before);
    });
  });
}

inline void Index::Append(TiledVector<OutputKV>&& entries, int height) {
  ages_[0]->Append(std::move(entries), height);
}

}  // namespace hornet::data::utxo
