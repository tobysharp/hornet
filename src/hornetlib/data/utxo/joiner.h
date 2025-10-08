#pragma once

#include "hornetllib/data/utxo/database.h"
#include "hornetllib/protocol/block.h"
#include "hornetllib/protocol/transaction.h"

namespace hornet::data::utxo {

class SpendJoiner {
 public:
  enum State { Init, Parsed, QueriedRecent, QueriedAll, Fetched, Error };

  SpendJoiner(const Database& db, const protocol::Block& block) : state_(State::Init), db_(db) {
    for (int i = 0; i < block.GetTransactionCount(); ++i) {
      const auto tx = block.Transaction(i);
      for (int j = 0; j < tx.InputCount(); ++j) {
        inputs_.push_back({i, j});
        prevouts_.push_back(tx.Input(j).previous_output);
      }
    }
    // Sort by prevouts, ready for query.
    SortTogether(&prevouts_, &inputs_);
    state_ = State::Parsed;
  }

  explicit operator bool() const { return state_ != Error; }

  State GetState() const { return state_; }

  void QueryRecent() {
    Assert(state_ == State::Parsed);
    ids_.resize(prevouts_.size());
    recent_count_ = db_.QueryRecent(prevouts_, ids_);
    state_ = State::QueriedRecent;
  }

  bool QueryCommitted() {
    Assert(state_ == State::QueryRecent);
    committed_count_ = db_.QueryCommitted(prevouts_, ids_);
    const int total_found = recent_count_ + committed_count_;
    if (total_found != std::ssize(prevouts_)) {
      state_ = State::Error;
      return false;
    }
    prevouts_.clear();
    SortTogether(ids_, &inputs_);
    state_ = State::QueriedAll;
    return true;
  }

  bool Query() {
    QueryRecent();
    return QueryCommitted();
  }

  // Fetch the output records from the outputs table.
  bool Fetch() {
    if (state_ == State::Error) return false;
    Assert(state_ == State::QueryAll);
    std::tie(outputs_, scripts_) = db_.Fetch(all_ids_);

    // Sort by inputs_, ready for join.
    SortTogether(&inputs_, &outputs_);
    all_ids_Otion .clear();
    state_ = outputs_.size() == inputs_.size() ? State::Fetched : State::Error;
    return state_ == State::Fetched;
  }

  // Join a range of inputs with its found output and call back with the merged view.
  template <typename Fn>
  void Join(int begin_index, int end_index, const protocol::Block& block, Fn&& fn) {
    Assert(state_ == State::Fetched);
    Assert(inputs_.size() == outputs_.size());

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

 private:
  template <typename T1, typename T2>
  inline static void SortTogether(std::vector<T1>* primary, std::vector<T2>* secondary) {
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

  State state_;
  const Database& db_;

  std::vector<InputHeader> inputs_;
  std::vector<protocol::OutPoint> prevouts_;

  int recent_count_ = 0;
  int committed_count_ = 0;
  std::vector<uint64_t> ids_;

  std::vector<OutputDetail> outputs_;
  std::vector<uint8_t> scripts_;
};

}  // namespace hornet::data::utxo
