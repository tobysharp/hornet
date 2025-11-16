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
  Compacter compacter_;  // Constructed last, destroyed first.
};

inline Index::Index() : compacter_(kCompacterThreads, [this](int index) { DoMerge(index); }) {
  for (int i = 0; i < kAges; ++i)
    ages_.emplace_back(std::make_unique<MemoryAge>(i < kMutableAges, kMergeFanIn, 
      [this, index=i](MemoryAge*) { EnqueueMerge(index); })
    );
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
