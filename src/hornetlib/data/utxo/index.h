#pragma once

#include <memory>
#include <numeric>
#include <vector>

#include "hornetlib/data/utxo/compacter.h"
#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/data/utxo/profiler.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class Index {
 public:
  class Pin {
   public:
    Pin();
    Pin(Index& index, int height);
    Pin(Pin&& rhs) noexcept;
    Pin(const Pin&) = delete;
    ~Pin();
    Pin& operator=(Pin&& rhs);
    Pin& operator=(const Pin&) = delete;
    void Reset();
   private:
    void Swap(Pin& rhs);
    Index* index_;
    int height_;
  };

  Index();

  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> ids, int since, int before) const;
  TiledVector<OutputKV> MakeAppendBuffer() const { return ages_[0]->MakeEntries(); }
  Pin Append(TiledVector<OutputKV>&& entries, int height);
  void EraseSince(int height);
  int GetContiguousLength() const;
  void WaitForContiguousLength(int height) const;
  bool ContainsHeight(int height) const;

  static constexpr int GetMutableWindow();
  static void SortKeys(std::span<OutputKey> keys);
  static void SortEntries(TiledVector<OutputKV>* entries);

 private:
  int ComputeContiguousLength() const;
  void UpdateContiguousLength(bool allow_decrease = false);
  void EnqueueMerge(int index) { compacter_.Enqueue(index); }
  void DoMerge(int index);

  static constexpr int kAges = 7;
  static constexpr int kMutableAges = 3;
  static constexpr int kCompacterThreads = kAges;
  static constexpr int kMergeFanIn = 8;
  
  std::vector<std::unique_ptr<MemoryAge>> ages_;
  std::atomic<int> contiguous_length_ = 0;
  mutable Compacter compacter_;  // Constructed last, destroyed first.
};

inline Index::Index() : compacter_(kCompacterThreads, [this](int index) { DoMerge(index); }) {
  for (int i = 0; i < kAges; ++i)
    ages_.emplace_back(std::make_unique<MemoryAge>(i < kMutableAges, kMergeFanIn, 
      [this, index=i](MemoryAge*) { EnqueueMerge(index); })
    );
  // Add an empty entry for the genesis block, which has no spendable outputs.
  ages_[0]->Append({}, std::make_pair(0, 1));
  UpdateContiguousLength();
}

inline void Index::DoMerge(int index) {
  if (index + 1 < std::ssize(ages_)) {
    ScopedProfiler profiler("Merge", index, "Age " + std::to_string(index) + " -> " + std::to_string(index + 1));
    ages_[index]->Merge(ages_[index + 1].get());
  }
}

inline QueryResult Index::Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const {
  Assert(std::is_sorted(keys.begin(), keys.end()));
  return std::accumulate(ages_.begin(), ages_.end(), QueryResult{}, [&](const QueryResult& sum, const auto& age) {
    // Note: If the queried age is immutable, it will throw an exception if height is within its data range.
    return sum + age->Query(keys, rids, since, before);
  });
}

inline Index::Pin Index::Append(TiledVector<OutputKV>&& entries, int height) {
  Assert(std::is_sorted(entries.begin(), entries.end()));
  Pin pin{*this, height};
  ages_[0]->Append(std::move(entries), {height, height + 1});
  UpdateContiguousLength();
  return pin;
}

inline void Index::EraseSince(int height) {
  const auto lock = compacter_.Lock();  // Serializes EraseSince with Merge calls.
  for (const auto& ptr : ages_)
    if (ptr->IsMutable()) ptr->EraseSince(height);
  UpdateContiguousLength(true);
}

inline int Index::GetContiguousLength() const {
  return contiguous_length_;
}

inline void Index::WaitForContiguousLength(int height) const {
  int current;
  while ((current = contiguous_length_) < height) contiguous_length_.wait(current);
}

// Returns the number of contiguously added blocks since genesis, before any holes.
inline int Index::ComputeContiguousLength() const {
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

inline void Index::UpdateContiguousLength(bool allow_decrease) {
  const int current = ComputeContiguousLength();
  for (int prev = contiguous_length_; allow_decrease || prev < current;) {
    if (contiguous_length_.compare_exchange_weak(prev, current)) {
      contiguous_length_.notify_all();
      break;
    }
  }
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

inline Index::Pin::Pin() : index_(nullptr), height_(-1) {
}

inline Index::Pin::Pin(Index& index, int height) : index_(&index), height_(height) {
  index_->ages_[kMutableAges - 1]->PinHeight(height_);
}

inline Index::Pin::Pin(Pin&& rhs) noexcept : Pin() {
  Swap(rhs);
}

inline Index::Pin::~Pin() {
  Reset();
}
    
inline Index::Pin& Index::Pin::operator=(Pin&& rhs) {
  if (this != &rhs) {
    Reset();
    Swap(rhs);
  }
  return *this;
}

inline void Index::Pin::Reset() {
  if (index_) index_->ages_[kMutableAges - 1]->UnpinHeight(height_);
  index_ = nullptr;
  height_ = -1;
}

inline void Index::Pin::Swap(Pin& rhs) {
  std::swap(index_, rhs.index_);
  std::swap(height_, rhs.height_);
}

}  // namespace hornet::data::utxo
