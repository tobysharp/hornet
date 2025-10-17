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
  struct Options {
    int skip_bits = 3;  // #Bits to skip from the key (e.g. already used by the shard partitioning).
    int prefix_bits = 10;     // #Bits to use to index the run's directory.
    int tile_bits = 13;       // log_2 of the tile size in KV entries.
    bool is_mutable = false;  // Whether to preserve information for undo.
  };

  MemoryRun(const Options& options)
      : options_(options),
        entries_(options.tile_bits),
        directory_(options.skip_bits, options.prefix_bits),
        height_range_(0, 0) {}

  bool Empty() const { return entries_.Empty(); }
  size_t Size() const { return entries_.Size(); }
  bool IsMutable() const { return is_mutable_; }
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  std::pair<int, int> HeightRange() const { return height_range_; }
  bool ContainsHeight(int height) const {
    return height_range_.first <= height && height < height_range_.second;
  }
  void EraseSince(int height);

  static MemoryRun Merge(std::span<std::shared_ptr<const MemoryRun>> inputs,
                         const Options& options);

 protected:
  int AddEntry(const OutputKV& kv, int next_bucket);

  const Options options_;
  TiledVector<OutputKV> entries_;
  Directory directory_;
  // TODO: Bloom filter.
  std::pair<int, int> height_range_;
};

inline int MemoryRun::Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const {
  // TODO: Check Bloom filter for quick exit.
  const auto [lo, hi] = directory_.LookupRange(keys[0]);
  return ForEachMatchInDoubleSorted(
      keys.begin(), keys.end(), entries_.begin() + lo, entries_.begin() + hi, entries_.end(),
      rids.begin(),
      [](const OutputId& rid) {
        return IdCodec::Length(rid) == 0
      },  // Only include queries that haven't already been found.
      [](const OutputKV& kv, OutputId* rid) {
        *rid = kv.rid;
        return kv.IsAdd();  // Only count `Add` records towards the total count returned.
      });
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

inline int MemoryRun::AddEntry(const OutputKV& kv, int next_bucket) {
  const int cur_bucket = directory_.GetBucket(kv.key);
  const int offset = entries_.Size();
  while (next_bucket <= cur_bucket) directory_[next_bucket++] = offset;
  entries_.PushBack(kv);
  return next_bucket;
}

// Multi-way streaming merge of sorted input runs to a single sorted output run.
inline MemoryRun MemoryRun::Merge(std::span<std::shared_ptr<const MemoryRun>> inputs,
                                  const Options& options) {
  using Iterator = typename decltype(entries_)::ConstIterator;
  struct Cursor {
    Iterator current, end;
    bool operator>(const Cursor& rhs) const { return *rhs.current < *current; }
  };

  // Initialize output.
  MemoryRun dst{options};

  // Initialize heap.
  std::priority_queue heap{std::vector<Cursor>{}, std::greater{}};
  for (const auto& run : inputs) heap.push({run->entries_.begin(), run->entries_.end()});

  int next_bucket = 0;
  std::optional<Iterator> prev;
  while (!heap.empty()) {
    auto cur = heap.top();
    heap.pop();
    if (prev.has_value()) {
      // If the current entry doesn't cancel out our deferred entry `prev`, then we add `prev` here.
      if (cur.current->IsDelete() || cur.current->key != (*prev)->key)
        next_bucket = AddEntry(**prev, next_bucket);
      prev.reset();
    }
    if (!is_mutable && cur.current->IsDelete())
      prev = cur.current;  // Defer adding this record so delete/add pairs are skipped.
    else
      next_bucket = AddEntry(*cur.current, next_bucket);
    if (++cur.current != cur.end) heap.push(cur);
  }
  // Flush any deferred value.
  if (prev.has_value()) next_bucket = AddEntry(**prev, next_bucket);

  // Finish directory.
  while (next_bucket < dst.directory_.Size()) dst.directory_[next_bucket++] = dst.entries_.Size();
  return dst;
}

}  // namespace hornet::data::utxo
