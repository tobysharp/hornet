#pragma once

#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class Index {
 public:
  Index(const std::filesystem::path& folder, int shard_bits = 9, int dictionary_bits = 7) :
    shard_bits_(shard_bits) {
    shards_.reserve(1 << shard_bits);
    for (int i = 0; i < 1 << shard_bits; ++i) shards_.emplace_back(shard_bits, dictionary_bits);
  }

  int ShardCount() const {
    return std::ssize(shards_);
  }

  int Query(std::span<const OutputKey> prevouts, std::span<OutputId> ids, int height) const;
  //void Append(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height);
  void EraseSince(int height);

 private:
  static std::pair<int, int> Partition(std::span<const OutputKey> keys, int bucket, int bits);

  const int shard_bits_;
  std::vector<Shard> shards_;
};

inline int Index::Query(std::span<const OutputKey> prevouts, std::span<OutputId> ids, int height) const {
  return ParallelSum(0, std::ssize(shards_), [&](int i) {
    const auto [lo, hi] = Partition(prevouts, i, shard_bits_);
    return shards_[i].Query(prevouts.subspan(lo, hi - lo), ids.subspan(lo, hi - lo), height);
  });
}

/* static */ inline std::pair<int, int> Index::Partition(std::span<const OutputKey> keys, int bucket, int bits) {
  const auto compare = [](const OutputKey& key, int b) {
    return static_cast<int>(GetLexicographicWord(key.hash, 0, bits)) < b;
  };
  const auto lo_it = std::lower_bound(keys.begin(), keys.end(), bucket, compare);
  const auto hi_it = std::lower_bound(lo_it, keys.end(), bucket + 1, compare);
  return { lo_it - keys.begin(), hi_it - keys.begin() };
}

}  // namespace hornet::data::utxo
