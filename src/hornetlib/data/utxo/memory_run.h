#pragma once

#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/directory.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class MemoryRun {
 public:
  MemoryRun(bool is_mutable = false) : is_mutable_(is_mutable) {}

  bool Empty() const { return entries_.Empty(); }
  size_t Size() const { return entries_.Size(); }
  bool IsMutable() const { return is_mutable_; }

  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;

  void EraseSince(int height);

 protected:
  TiledVector<OutputKV> entries_;
  Directory directory_;
  // TODO: Bloom filter.
  std::pair<int, int> height_range_;
  const bool is_mutable_ = false;
};

inline int MemoryRun::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  // TODO: Check Bloom filter for quick exit.
  const auto [lo, hi] = directory_.LookupRange(keys[0]);
  return ForEachMatchInDoubleSorted(keys.begin(), keys.end(), entries_.begin() + lo, entries_.begin() + hi, entries_.end(), rids.begin(),
    [](const OutputId& rid) { return IdCodec::Length(rid) == 0},  // Only include queries that haven't already been found.
    [](const OutputKV& kv, OutputId* rid) {
      *rid = kv.rid;
      return kv.IsAdd();  // Only count `Add` records towards the total count returned.
    }
  );
}

inline bool MemoryRun::EraseSince(int height) {
  Assert(IsMutable());
  if (height <= height_range_.first) {
    // Entire run is removed.
    entries_.Clear();
    directory_.Clear();
    height_range_ = {0, 0};
    return true;  // Signal run may be removed by container.
  } else if (height < height_range_.second) {
    // Run partially overlaps with undo range.
    entries_.EraseIf([&](const OutputKV& kv) { return kv.height >= height; });
    directory_.Rebuild(entries_);
    // TODO: Optionally rebuild Bloom filter.
    height_range_.second = height;
  }
  return false;
}

}  // namespace hornet::data::utxo
