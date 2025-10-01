#include <algorithm>
#include <expected>
#include <span>
#include <unordered_map>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/utxo/compact_index.h"
#include "hornetlib/data/utxo/outputs_table.h"
#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/data/utxo/streaming_unspent_state.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

namespace {


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

  // Query all the input prevouts to check they exist as unspent outputs.
  void QueryUnspent(const protocol::Block&) {}

 protected:
  std::vector<UnspentShard> shards_;
};

}  // namespace

class StreamingUnspentState::Impl {
 public:
  struct Info {};

  // Retrieve stats on the internal state of this object.
  Info GetInfo() const;

  // Mark input prevouts as spent and add new outputs.
  // May use cached mutable state from the previous EnumerateUnspentImpl to save duplicated work.
  void ConnectBlock(const protocol::Block& block);

  // Explicitly compact the representation, which may be an expensive operation.
  void Compact();

  template <typename Fn>
  consensus::Result QueryUnspent(const protocol::Block&, Fn&&) {
    return {};
  }

 private:
  OutputsTable outputs_;
  UnspentIndex unspent_;
};

consensus::Result StreamingUnspentState::EnumerateUnspentImpl(const protocol::Block& block,
                                                              const Callback cb,
                                                              const void* user) const {
  return impl_->QueryUnspent(block, [&](int tx_index, int input_index, const auto& record) {
    return (*cb)(tx_index, input_index, record, user);
  });
}

}  // namespace hornet::data::utxo