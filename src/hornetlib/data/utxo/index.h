#pragma once

#include <memory>
#include <numeric>
#include <vector>

#include "hornetlib/data/utxo/compacter.h"
#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/data/utxo/parallel.h"
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

  static void SortKeys(std::span<OutputKey> keys);
  static void SortEntries(TiledVector<OutputKV>* entries);

 private:
  struct QueryRange {
    std::span<const OutputKey> keys;
    std::span<OutputId> rids;
  };

  static std::vector<QueryRange> SplitQuery(std::span<const OutputKey> keys, std::span<OutputId> ids, int splits);

  // Called with each Append according to the mutability window.
  void SetMutableSince(int height) {
    // Set the merge policy for the last mutable age before immutability occurs.
    for (auto it = ages_.rbegin(); it != ages_.rend(); ++it)
      if ((*it)->IsMutable()) (*it)->RetainSince(height);
  }

  void EnqueueMerge(int index) { compacter_.Enqueue(index); }
  void DoMerge(int index);

  std::vector<std::unique_ptr<MemoryAge>> ages_;
  Compacter compacter_;

  static constexpr int kAges = 7;
  static constexpr int kMutableAges = 3;
  static constexpr int kCompacterThreads = kAges;
  static constexpr int kMergeFanIn = 8;
};

Index::Index() : compacter_(kCompacterThreads, [this](int index) { DoMerge(index); }) {
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
  static constexpr int kRanges = 8;
  return ParallelSum<QueryResult>(SplitQuery(keys, rids, kRanges), {}, [&](const QueryRange& range) {
    return std::accumulate(ages_.begin(), ages_.end(), QueryResult{}, [&](const QueryResult& sum, const auto& age) {
      // Note: If the queried age is immutable, it will throw an exception if height is within its data range.
      return sum + age->Query(range.keys, range.rids, since, before);
    });
  });
}

/* static */ inline std::vector<Index::QueryRange> Index::SplitQuery(std::span<const OutputKey> keys, std::span<OutputId> rids, int splits) {
  Assert(keys.size() == rids.size());
  std::vector<QueryRange> ranges(splits);
  const size_t size = keys.size();
  size_t cursor = 0;
  for (int i = 0; i < splits; ++i)
  {
    const size_t next = (i + 1) * size / splits;
    ranges[i] = { keys.subspan(cursor, next - cursor), rids.subspan(cursor, next - cursor) };
    cursor = next;
  }
  return ranges;
}

inline void Index::Append(TiledVector<OutputKV>&& entries, int height) {
  ages_[0]->Append(std::move(entries), {height, height + 1});
}

/* static */ inline void Index::SortKeys(std::span<OutputKey> keys) {
  ParallelSort(keys.begin(), keys.end());
}

/* static */ inline void Index::SortEntries(TiledVector<OutputKV>* entries) {
  ParallelSort(entries->begin(), entries->end());
}

}  // namespace hornet::data::utxo
