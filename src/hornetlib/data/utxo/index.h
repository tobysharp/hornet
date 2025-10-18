#pragma once

#include "hornetlib/data/utxo/shard.h"

namespace hornet::data::utxo {

class Index {
 public:
  Index(const std::filesystem::path& folder, int shard_bits = 9, int dictionary_bits = 7) {
    shards_.reserve(1 << shard_bits);
    for (int i = 0; i < 1 << shard_bits; ++i) shards_.emplace_back(shard_bits, dictionary_bits);
  }

  int ShardCount() const {
    return std::ssize(shards_);
  }

  static void SortKeys(std::span<OutputKey> prevouts);

  int QueryRecent(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  int QueryMain(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  void AddAndDeleteOutputs(const protocol::Block& block, std::span<const OutputKV> add_outputs_kvs, int height);
  void EraseSince(int height);

 private:
  static void SortKeys(std::span<OutputKV> kvs);

  std::vector<Shard> shards_;
};

}  // namespace hornet::data::utxo
