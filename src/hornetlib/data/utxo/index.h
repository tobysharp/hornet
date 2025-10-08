#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class Index {
 public:
  Index(const std::filesystem::path& folder, int shard_bits = 9, int dictionary_bits = 7) {
    shards_.reserve(1 << shard_bits);
    for (int i = 0; i < 1 << shard_bits; ++i)
      shards_.emplace_back(shard_bits, dictionary_bits);
  }

  int ShardCount() const {
    return std::ssize(shards_);
  }

  int QueryTail(std::span<const OutputKey> prevouts,
                                 std::span<OutputId> ids) const;
  int QueryMain(std::span<const OutputKey> prevouts,
                                    std::span<OutputId> ids) const;
 
  void AppendTail(std::span<const OutputKV> pairs, int height);

  void RemoveSince(int height);

  int GetEarliestTailHeight() const;

  void CommitBefore(int height);

 protected:
  class Shard {

  };

  std::vector<Shard> shards_;   // The shards of the main index.
  std::vector<OutputKV> tail_;  // The in-memory sorted tail.
};

}  // namespace hornet::data::utxo
