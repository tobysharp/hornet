#pragma once

#include <memory>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/cmopacter.h"
#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class Index {
 public:
  Index(const std::filesystem::path& folder);

  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> ids, int since, int before, bool skip_found) const;
  TiledVector<OuptutKV> MakeAppendBuffer() const { return ages_[0]->MakeEntries(); }
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
};

inline QueryResult Index::Query(std::span<const OutputKey> keys, std::span<OutputId> ids, int since, int before, bool skip_found) const {
  static constexpr int kRanges = 8;
  return ParallelSum(SplitQuery({keys, ids}, kRanges), [&](const auto& range) {
    return std::accumulate(ages_.begin(), ages_.end(), QueryResult{}, [&](const QueryResult& sum, const auto& age) {
      // Note: If the queried age is immutable, it will throw an exception if height is within its data range.
      return sum + age->Query(range.keys, range.ids, since, before, skip_found);
    });
  });
}

inline void Index::Append(TiledVector<OutputKV>&& entries, int height) {
  ages_[0]->Append(MemoryRun::Create(std::move(entries), height));
}

}  // namespace hornet::data::utxo
