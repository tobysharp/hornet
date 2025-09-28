#include <algorithm>
#include <expected>
#include <span>
#include <unordered_map>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/utxo/compact_index.h"
#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/data/utxo/streaming_unspent_state.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data {

namespace {

using OutputRecord = consensus::UnspentDetail;

// Append-only storage for full outputs. Disk-backed, segmented, mmap for reads.
// Copy-merge for compaction in the background, per-segment prioritized by least efficient first.
class OutputsTable {
 public:
  void AppendOutputs(const protocol::Block& block, int height);
  uint64_t AppendOutput(const OutputRecord& record);

  // For each {segment, offset} index in [begin, end), call fn with an OutputRecord.
  template <typename Iter, typename Fn>
  void ForEachOutput(Iter begin, Iter end, Fn&& fn);

 private:
  std::vector<uint8_t> data_;
};

uint64_t OutputsTable::AppendOutput(const OutputRecord& record) {
  const uint64_t start = data_.size();
  const uint8_t* precord = reinterpret_cast<const uint8_t*>(&record);
  const uint8_t* pscript = reinterpret_cast<const uint8_t*>(&record.script);
  data_.insert(data_.end(), precord, pscript);
  data_.push_back(static_cast<uint32_t>(record.script.size()));
  data_.insert(data_.end(), record.script.begin(), record.script.end());
  return start;
}

void OutputsTable::AppendOutputs(const protocol::Block& block, int height) {
  for (const auto& tx : block.Transactions()) {
    for (int i = 0; i < tx.OutputCount(); ++i) {
      const auto& output = tx.Output(i);
      AppendOutput(OutputRecord{height, 0, output.value, tx.PkScript(i)});
    }
  }
}

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
  return impl_->QueryUnspent(block, [&](int tx_index, int input_index, const OutputRecord& record) {
    return (*cb)(tx_index, input_index, record, user);
  });
}

}  // namespace hornet::data