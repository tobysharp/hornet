#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class UnspentIndex {
 public:
  UnspentIndex(int shard_bits = 9, int dictionary_bits = 7) {
    shards_.reserve(1 << shard_bits);
    for (int i = 0; i < 1 << shard_bits; ++i)
      shards_.emplace_back(shard_bits, dictionary_bits);
  }

  int ShardCount() const {
    return std::ssize(shards_);
  }

  struct QueryResult {
    int tx_index;
    int input_index;
    uint64_t address;
  };
  using QueryResults = std::vector<QueryResult>;

  // Query all the input prevouts to check they exist as unspent outputs.
  std::optional<QueryResults> QueryAllUnspent(const protocol::Block& block);

 protected:
  std::vector<UnspentShard> shards_;
};

}  // namespace hornet::data::utxo
