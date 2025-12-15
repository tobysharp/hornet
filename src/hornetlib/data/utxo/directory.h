#pragma once

#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class Directory {
 public:
  Directory(int prefix_bits)
  : prefix_bits_(prefix_bits), entries_((1 << prefix_bits) + 1) {}
  template <typename Iter>
  Directory(int prefix_bits, Iter kv_begin, Iter kv_end);

  int Size() const {
    return std::ssize(entries_);
  }
  
  int GetBucket(const OutputKey& key) const {
    uint32_t le_word;
    std::memcpy(&le_word, key.hash.data(), sizeof(le_word));
    const uint32_t be_word = __builtin_bswap32(le_word);
    return be_word >> (32 - prefix_bits_);
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
  template <typename Iter>
  void Rebuild(Iter kv_begin, Iter kv_end);

 protected:
  const int prefix_bits_;
  std::vector<uint32_t> entries_;
};

template <typename Iter>
inline Directory::Directory(int prefix_bits, Iter kv_begin, Iter kv_end) 
  : prefix_bits_(prefix_bits), entries_((1 << prefix_bits) + 1) {
  Rebuild(kv_begin, kv_end);
}

template <typename Iter>
inline void Directory::Rebuild(Iter kv_begin, Iter kv_end) {
  auto it = kv_begin;
  for (int i = 0; i < std::ssize(entries_); ++i) {
    // We want the bucket to index the first kv pair whose key prefix falls into this bucket.
    // If there are no such entries, the bucket is empty.
    it = std::lower_bound(it, kv_end, i, [&](const OutputKV& kv, int bucket) {
      return GetBucket(kv.key) < bucket;
    });
    entries_[i] = it - kv_begin;
  }
}

}  // namespace hornet::data::utxo
