#pragma once

#include <span>
#include <vector>

#include "hornetlib/consensus/utxo.h"
#include "hornetlib/data/utxo/index.h"
#include "hornetlib/data/utxo/outputs_table.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

template <typename T1, typename T2>
inline void SortTogether(std::vector<T1>* primary, std::vector<T2>* secondary) {
  std::vector<int> indices(primary->size());
  std::iota(indices.begin(), indices.end(), 0);
  std::sort(indices.begin(), indices.end(), [&](int a, int b) { return (*primary)[a] < (*primary)[b]; });

  auto primary_copy = *primary;
  auto secondary_copy = *secondary;
  for (int i = 0; i < std::ssize(indices); ++i) {
    (*primary)[i] = primary_copy[indices[i]];
    (*secondary)[i] = secondary_copy[indices[i]];
  }
}

// This class represents a specialized, stateful sort-merge join operator for UTXOs.
// SpendingRecords = Join(BlockInputs ⋈ UnspentIndex ⋈ OutputsTable)
class SpendJoiner {
 public:
  enum State { Init, Queried, Fetched, Error };

  // Construct the query from a block
  SpendJoiner(const protocol::Block& block) : state_(State::Init) {
    for (int i = 0; i < block.GetTransactionCount(); ++i) {
      const auto tx = block.Transaction(i);
      for (int j = 0; j < tx.InputCount(); ++j) {
        inputs_.push_back({i, j});
        prevouts_.push_back(tx.Input(j).previous_output);
      }
    }
    // Sort by prevouts, ready for query.
    SortTogether(&prevouts_, &inputs_);
  }

  // Execute the query against the outputs index. Returns true iff all records found unspent.
  bool Query(const UnspentIndex& index) {
    Assert(state_ == State::Init);
    const auto result = index.QueryAllUnspent(prevouts_);
    if (!result) {
      state_ = State::Error;
      return false;
    }
    addresses_ = std::move(*result);
    // TODO: Handle errors

    // Sort by addresses_, ready for fetch.
    SortTogether(&addresses_, &inputs_);
    prevouts_.clear();
    state_ = State::Queried;
    return true;
  }

  // Fetch the output records from the outputs table.
  void Fetch(const OutputsTable& table) {
    Assert(state_ == State::Queried);
    std::tie(outputs_, scripts_) = table.Fetch(addresses_);

    // Sort by inputs_, ready for join.
    SortTogether(&inputs_, &outputs_);
    addresses_.clear();
    state_ = State::Fetched;
  }

  // Join a range of inputs with its found output and call back with the merged view.
  template <typename Fn>
  void Join(int begin_index, int end_index, const protocol::Block& block, Fn&& fn) {
    Assert(state_ == State::Fetched);
    for (int index = begin_index; index < end_index; ++index) {
      const OutputDetail& detail = outputs_[index];
      const OutputHeader& header = detail.header;
      const consensus::SpendRecord spend{ 
        .funding_height = header.height,
        .funding_flags = header.flags,
        .amount = header.amount,
        .pubkey_script = detail.script.Span(scripts_),
        .tx = block.Transaction(inputs_[index].tx_index),
        .spend_input_index = inputs_[index].input_index
      };
      fn(spend);
    }
  }

 protected:
  State state_;

  std::vector<InputHeader> inputs_;
  std::vector<protocol::OutPoint> prevouts_;

  std::vector<uint64_t> addresses_;

  std::vector<OutputDetail> outputs_;
  std::vector<uint8_t> scripts_;
};

}  // namespace hornet::data::utxo
