#pragma once

#include <memory>
#include <numeric>
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
  int GetContiguousLength() const;
  bool ContainsHeight(int height) const;

  static constexpr int GetMutableWindow();
  static void SortKeys(std::span<OutputKey> keys);
  static void SortEntries(TiledVector<OutputKV>* entries);

 private:
  void EnqueueMerge(int index) { compacter_.Enqueue(index); }
  void DoMerge(int index);

  static constexpr int kAges = 7;
  static constexpr int kMutableAges = 3;
  static constexpr int kCompacterThreads = kAges;
  static constexpr int kMergeFanIn = 8;
  
  std::vector<std::unique_ptr<MemoryAge>> ages_;
  mutable Compacter compacter_;  // Constructed last, destroyed first.
};

inline Index::Index() : compacter_(kCompacterThreads, [this](int index) { DoMerge(index); }) {
  for (int i = 0; i < kAges; ++i)
    ages_.emplace_back(std::make_unique<MemoryAge>(i < kMutableAges, kMergeFanIn, 
      [this, index=i](MemoryAge*) { EnqueueMerge(index); })
    );
  // Add an empty entry for the genesis block, which has no spendable outputs.
  ages_[0]->Append({}, std::make_pair(0, 1));
}

inline void Index::DoMerge(int index) {
  if (index + 1 < std::ssize(ages_))
    ages_[index]->Merge(ages_[index + 1].get());
}

inline QueryResult Index::Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const {
  Assert(std::is_sorted(keys.begin(), keys.end()));
  return std::accumulate(ages_.begin(), ages_.end(), QueryResult{}, [&](const QueryResult& sum, const auto& age) {
    // Note: If the queried age is immutable, it will throw an exception if height is within its data range.
    return sum + age->Query(keys, rids, since, before);
  });
}

inline void Index::Append(TiledVector<OutputKV>&& entries, int height) {
  Assert(std::is_sorted(entries.begin(), entries.end()));
  ages_[0]->Append(std::move(entries), {height, height + 1});
}

inline void Index::EraseSince(int height) {
  const auto lock = compacter_.Lock();  // Serializes EraseSince with Merge calls.
  for (const auto& ptr : ages_)
    if (ptr->IsMutable()) ptr->EraseSince(height);
}

inline int Index::GetContiguousLength() const {
  //const auto lock = compacter_.Lock();
  // This lock-free implementation requires to search the ages in increasing maturity.

  std::optional<int> age0_min, age0_min_pre_hole;
  {
    const auto age0 = ages_[0]->RunsSnapshot();
    if (!age0->empty())
    {
      age0_min = age0->front()->HeightRange().first;
      age0_min_pre_hole = *age0_min - 1;
      for (const auto& run : *age0) {
        if (age0_min_pre_hole != run->HeightRange().first - 1)
          break;
        (*age0_min_pre_hole)++;
      }
    }
  }

  std::optional<int> older_max;
  for (int i = 1; i < std::ssize(ages_); ++i) {
    const auto runs = ages_[i]->RunsSnapshot();
    if (!runs->empty()) {
      older_max = runs->back()->HeightRange().second - 1;
      break;
    }
  }

  // If the first height in age 0 joins up with the previous ages, we don't have a gap there.
  if (age0_min && (!older_max || *older_max + 1 >= *age0_min))
      return *age0_min_pre_hole + 1;
  // Otherwise there is a hole at the start of age 0.
  else if (older_max)
      return *older_max + 1;
  else
    return 0;
}

inline bool Index::ContainsHeight(int height) const {
  for (const auto& age : ages_)
    if (age->ContainsHeight(height)) return true;
  return false;
}

/* static */ inline void Index::SortKeys(std::span<OutputKey> keys) {
  ParallelSort(keys.begin(), keys.end());
}

/* static */ inline void Index::SortEntries(TiledVector<OutputKV>* entries) {
  ParallelSort(entries->begin(), entries->end());
}

/* static */ inline constexpr int Index::GetMutableWindow() { 
  int count = 0;
  int k = kMergeFanIn;
  for (int i = 0; i < kMutableAges; ++i) {
    count += k;
    k *= kMergeFanIn;
  }
  return count;
}

}  // namespace hornet::data::utxo
