#pragma once

#include <cstdint>
#include <optional>
#include <tuple>
#include <queue>
#include <vector>

#include "hornetlib/data/utxo/directory.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/util/assert.h"

namespace hornet::data::utxo {

class MemoryRun {
 public:
  struct Options {
    int prefix_bits = 10;     // #Bits to use to index the run's directory.
    int tile_bits = 13;       // log_2 of the tile size in KV entries.
    bool is_mutable = false;  // Whether to preserve information for undo.
  };

  MemoryRun(const Options& options)
      : options_(options),
        entries_(options.tile_bits),
        directory_(options.prefix_bits) {}

  MemoryRun(const Options& options, TiledVector<OuptutKV>&& entries)
      : options_(options), entries_(std::move(entries)), directory_(options.prefix_bits, entries_) {}
    
  bool Empty() const { return entries_.Empty(); }
  size_t Size() const { return entries_.Size(); }
  bool IsMutable() const { return options_.is_mutable; }
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids) const;
  std::pair<int, int> HeightRange() const { return height_range_; }
  bool ContainsHeight(int height) const {
    return height_range_.first <= height && height < height_range_.second;
  }
  void EraseSince(int height);

  static MemoryRun Merge(const Options& options, std::span<std::shared_ptr<const MemoryRun>> inputs);
  static MemoryRun Create(const Options& options, std::span<const OutputKV> adds, std::span<const OutputKey> deletes, int height);

 protected:
  int AddEntry(const OutputKV& kv, int next_bucket);

  const Options options_;
  TiledVector<OutputKV> entries_;
  Directory directory_;
  // TODO: Bloom filter.
  std::pair<int, int> height_range_ = { std::numeric_limits<int>::max(), std::numeric_limits<int>::min() };
};

inline QueryResult MemoryRun::Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before, bool skip_found) const {
  if (before <= height_range_.first || height_range_.second <= since) return {0, 0};  // No overlap

  // In an immutable run, we can only guarantee correct results if the entire run is contained within the queried time range.
  if (!IsMutable() && before < height_range_.second) 
    util::ThrowInvalidArgument("Queried height already merged to immutable.");

  // TODO: Check Bloom filter for quick exit.

  int adds = 0, deletes = 0;
  const int size = std::ssize(keys);
  const auto order = [](const auto& lhs, const auto& rhs) { return lhs <=> rhs; };
  const auto match = [since, before](const OutputKey& key, const OutputKV& entry) { 
    return key == entry.key && since <= entry.height && entry.height < before;
  };
  auto lower = entries_.begin(), upper = entries_.end();
  for (int index = 0; index < size; ++index) {
    // Skip queries that are filtered out by the output destination.
    if (skip_found && rids[index] != kNullOutputId) continue;

    // Get the key for this query.
    const auto& key = keys[index];
    
    // Tighten bounds when available externally (e.g. directory).
    const auto [lo, hi] = directory_.LookupRange(key);
    lower = std::max(lower, entries_.begin() + lo);  // Lower bound is monotonically increasing...
    upper = entries_.begin() + hi;                   // while upper bound resets for each key.

    // Tighten bounds again by galloping forwards until we pass over the key.
    std::tie(lower, upper) = GallopingRangeSearch(lower, upper, key, order);

    // Binary search within the tighter window for the first exact match, if any.
    const auto found = BinarySearchFirst(lower, upper, key, order, match);

    // Write the value to the output.
    if (found != upper) {
      if (found->op == OutputKV::Add) {
        rids[index] = found->rid;
        ++adds;
      }
      else ++deletes;
    }
  }
  return {adds, deletes};
}

inline void MemoryRun::EraseSince(int height) {
  Assert(IsMutable());
  Assert(ContainsHeight(height));

  // Run partially overlaps with undo range.
  entries_.EraseIf([&](const OutputKV& kv) { return kv.height >= height; });
  directory_.Rebuild(entries_);
  // TODO: Optionally rebuild Bloom filter.
  height_range_.second = height;
}

inline int MemoryRun::AddEntry(const OutputKV& kv, int next_bucket) {
  const int cur_bucket = directory_.GetBucket(kv.key);
  const int offset = entries_.Size();
  while (next_bucket <= cur_bucket) directory_[next_bucket++] = offset;
  entries_.PushBack(kv);
  return next_bucket;
}

// Multi-way streaming merge of sorted input runs to a single sorted output run.
inline MemoryRun MemoryRun::Merge(const Options& options, std::span<std::shared_ptr<const MemoryRun>> inputs) {
  using Iterator = typename decltype(entries_)::ConstIterator;
  struct Cursor {
    Iterator current, end;
    bool operator >(const Cursor& rhs) const { return *rhs.current < *current; }
  };

  // Initialize output.
  MemoryRun dst{options};

  // Initialize heap and destination height range.
  std::priority_queue<Cursor> heap{std::greater{}};
  for (const auto& run : inputs) {
    heap.push({run->entries_.begin(), run->entries_.end()});
    dst.height_range_.first = std::min(dst.height_range_.first, run->height_range_.first);
    dst.height_range_.second = std::max(dst.height_range_.second, run->height_range_.second);
  }

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
    if (!dst.IsMutable() && cur.current->IsDelete())
      prev = cur.current;  // Defer adding this record so delete/add pairs are skipped.
    else
      next_bucket = AddEntry(*cur.current, next_bucket);
    if (++cur.current != cur.end) heap.push(cur);
  }
  // Flush any deferred value.
  if (prev.has_value()) next_bucket = AddEntry(**prev, next_bucket);

  // Finish directory.
  while (next_bucket < dst.directory_.Size()) dst.directory_[next_bucket++] = dst.entries_.Size();

  // TODO: Create Bloom filter.
  return dst;
}

/* static */ inline MemoryRun MemoryRun::Create(const Options& options, TiledVector<OutputKV>&& entries, int height) {
  MemoryRun dst{options};
  for (const OutputKV& kv : adds) dst.entries_.PushBack(kv);
  for (const OutputKey& key : deletes) dst.entries_.PushBack({key, OutputKV::Delete});
  dst.directory_.Rebuild(dst.entries_);
  // TODO: Create Bloom filter.
  return dst;
}

}  // namespace hornet::data::utxo
