#pragma once

#include <bit>
#include <cstdint>
#include <optional>
#include <tuple>
#include <queue>
#include <vector>

#include "hornetlib/data/utxo/directory.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/search.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/util/assert.h"

namespace hornet::data::utxo {

class MemoryRun {
 public:
  MemoryRun(bool is_mutable, int prefix_bits)
      : is_mutable_(is_mutable), directory_(prefix_bits) {}
  MemoryRun(const MemoryRun& rhs);
  MemoryRun(bool is_mutable, TiledVector<OutputKV>&& entries, const std::pair<int, int>& height_range = { std::numeric_limits<int>::max(), std::numeric_limits<int>::min() })
      : is_mutable_(is_mutable), entries_(std::move(entries)), directory_(ComputePrefixBits(entries_.Size()), entries_.begin(), entries_.end()), height_range_(height_range) {
        // TODO: Create Bloom filter.
      }
    
  bool Empty() const { return entries_.Empty(); }
  size_t Size() const { return entries_.Size(); }
  bool IsMutable() const { return is_mutable_; }
  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const;
  std::pair<int, int> HeightRange() const { return height_range_; }
  bool ContainsHeight(int height) const {
    return height_range_.first <= height && height < height_range_.second;
  }
  void EraseSince(int height);
  
  auto Begin() const { return entries_.begin(); }
  auto End() const { return entries_.end(); }

  static MemoryRun Merge(bool is_mutable, std::span<std::shared_ptr<const MemoryRun>> inputs);

 protected:
  int AddEntry(const OutputKV& kv, int next_bucket);

  static int ComputePrefixBits(int entries) {
    constexpr int kMinPrefixBits = 4;
    constexpr int kTargetBytesPerBucket = 4096;
    constexpr int kEntriesPerBucket = kTargetBytesPerBucket / sizeof(OutputKV);
    const int buckets = (entries + kEntriesPerBucket - 1) / kEntriesPerBucket;
    const int prefix_bits = buckets <= 0 ? 0 : std::bit_width(static_cast<unsigned int>(buckets - 1));
    return std::max(kMinPrefixBits, prefix_bits);
  }

  const bool is_mutable_;
  TiledVector<OutputKV> entries_;
  Directory directory_;
  // TODO: Bloom filter.
  std::pair<int, int> height_range_ = { std::numeric_limits<int>::max(), std::numeric_limits<int>::min() };
};

inline MemoryRun::MemoryRun(const MemoryRun& rhs) 
  : is_mutable_(rhs.is_mutable_), entries_(rhs.entries_), directory_(rhs.directory_), height_range_(rhs.height_range_) {
}

inline QueryResult MemoryRun::Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const {
  if (before <= height_range_.first || height_range_.second <= since) return {0, 0};  // No overlap

  // In an immutable run, we can only guarantee correct results if the entire run is contained within the queried time range.
  if (!IsMutable() && before < height_range_.second) 
    util::ThrowInvalidArgument("Queried height already merged to immutable.");

  // TODO: Check Bloom filter for quick exit.

  int adds = 0, deletes = 0;
  const int size = std::ssize(keys);
  const auto order = [](const auto& lhs, const auto& rhs) { return lhs <=> rhs; };
  const auto match = [since, before](const OutputKey& key, const OutputKV& entry) { 
    return key == entry.key && since <= entry.data.height && entry.data.height < before;
  };
  // We can skip over previously found rid's if we can guarantee we won't find a newer entry here
  // than one that was found previously, i.e. if we're searching from genesis.
  const bool skip_found = since == 0;
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
      if (found->data.op == OutputKV::Add) {
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
  entries_.EraseIf([&](const OutputKV& kv) { return kv.data.height >= height; });
  directory_.Rebuild(entries_.begin(), entries_.end());
  // TODO: Optionally rebuild Bloom filter.
  height_range_.second = height;
}

inline int MemoryRun::AddEntry(const OutputKV& kv, int bucket) {
  const int cur_bucket = directory_.GetBucket(kv.key);
  const int offset = entries_.Size();
  while (bucket <= cur_bucket) directory_[bucket++] = offset;
  entries_.PushBack(kv);
  return bucket;
}

// Multi-way streaming merge of sorted input runs to a single sorted output run.
inline MemoryRun MemoryRun::Merge(bool is_mutable, std::span<std::shared_ptr<const MemoryRun>> inputs) {
  using Iterator = typename decltype(entries_)::ConstIterator;
  struct Cursor {
    Iterator current, end;
    bool operator >(const Cursor& rhs) const { return *rhs.current < *current; }
  };

  // Compute the number of prefix bits to use for the directory, based on an upper bound for the size of the run.
  const int approx_entries = std::accumulate(inputs.begin(), inputs.end(), 0, [&](int sum, const auto& run) {
    return sum + run->Size();
  });
  const int prefix_bits = ComputePrefixBits(approx_entries);

  // Initialize output.
  MemoryRun dst{is_mutable, prefix_bits};

  // Initialize heap and destination height range.
  std::priority_queue<Cursor, std::vector<Cursor>, std::greater<Cursor>> heap;
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
    bool cancel = false;
    if (prev.has_value()) {
      // If the current entry doesn't cancel out our deferred entry `prev`, then we add `prev` here.
      cancel = cur.current->IsAdd() && cur.current->key == (*prev)->key;
      if (!cancel) 
        next_bucket = dst.AddEntry(**prev, next_bucket);
      prev.reset();
    }
    if (!dst.IsMutable() && cur.current->IsDelete())
      prev = cur.current;  // Defer adding this record so delete/add pairs are skipped.
    else if (!cancel)
      next_bucket = dst.AddEntry(*cur.current, next_bucket);
    if (++cur.current != cur.end) heap.push(cur);
  }
  // Flush any deferred value.
  if (prev.has_value()) next_bucket = dst.AddEntry(**prev, next_bucket);

  // Finish directory.
  while (next_bucket < dst.directory_.Size()) dst.directory_[next_bucket++] = dst.entries_.Size();

  // TODO: Create Bloom filter.
  return dst;
}

}  // namespace hornet::data::utxo
