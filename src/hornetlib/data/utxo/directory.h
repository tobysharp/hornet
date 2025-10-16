#pragma once

#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include "hornetilb/data/utxo/types.h"
#include "hornetlib/data/utxo/tiled_vector.h"

namespace hornet::data::utxo {

class Directory {
 public:
  Directory(int skip_bits, int prefix_bits)
  : skip_bits_(skip_bits), prefix_bits_(prefix_bits), entries_((1 << prefix_bits) + 1) {}
  Directory(int skip_bits, int prefix_bits, std::vector<uint32_t>&& entries)
  : skip_bits_(skip_bits), prefix_bits_(prefix_bits), entries_(std::move(entries)) {
    Assert(entries.size() == (1 << prefix_bits) + 1);
  }
  Directory(const TiledVector<OutputKV>& kvs);

  int Size() const {
    return std::ssize(entries_);
  }
  int GetBucket(const OutputKey& key) const {
    return static_cast<int>(GetLexicographicWord(key.hash, skip_bits_, prefix_bits_));
  }
  std::pair<uint32_t, uint32_t> LookupRange(const OutputKey& key) const {
    const int index = GetBucket(key);
    return {entries_[index], entries_[index + 1]};    
  }
  uint32_t& operator[](int index) {
    return entries_[index];
  }
  uint32_t operator[](int index) const {
    return entries_[index];
  }
  void Rebuild(const TiledVector<OutputKV>& kvs);

 protected:
  const int skip_bits_, prefix_bits_;
  std::vector<uint32_t> entries_;
};

inline Directory::Directory(int skip_bits, int prefix_bits, const TiledVector<OutputKV>& kvs) 
  : skip_bits_(skip_bits), prefix_bits_(prefix_bits), entries_((1 << prefix_bits) + 1) {
  Rebuild(kvs);
}

inline void Directory::Rebuild(const TiledVector<OutputKV>& kvs) {
  auto it = kvs.begin();
  for (int i = 0; i < std::ssize(entries_); ++i) {
    // Find the first entry for which the bucket index >= i.
    it = std::lower_bound(it, kvs.end(), i, [&](const OutputKV& kv, int bucket) {
      return GetBucket(kv.key) < bucket;
    });
    entries_[i] = it - kvs.begin();
  }
}

}  // namespace hornet::data::utxo
