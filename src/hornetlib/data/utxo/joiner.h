#pragma once

#include <cstdint>
#include <vector>

#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/sort.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class SpendJoiner {
 public:
  enum State { Init, Parsed, Appended, Queried, Fetched, Joined, Error };

  using Callback = std::function<void(const consensus::SpendRecord&)>;

  SpendJoiner(Database& db, 
              std::shared_ptr<const protocol::Block> block, 
              int height,
              Callback callback) 
              : state_(State::Init), db_(db), block_(block), height_(height), callback_(callback) {
    Parse();
  }

  explicit operator bool() const { return state_ != Error; }
  State GetState() const { return state_; }
  bool Advance();

 private:
  void Parse();
  void Append();
  bool Query();
  bool Fetch();
  void Join();

  State state_;
  Database& db_;
  std::shared_ptr<const protocol::Block> block_;
  const int height_;
  const Callback callback_;
  std::vector<InputHeader> inputs_;
  std::vector<OutputKey> keys_;
  std::vector<OutputId> rids_;
  std::vector<OutputDetail> outputs_;
  std::vector<uint8_t> scripts_;
};

inline void SpendJoiner::Parse() {
  Assert(state_ == State::Init);
  for (int i = 0; i < block_->GetTransactionCount(); ++i) {
    const auto tx = block->Transaction(i);
    for (int j = 0; j < tx.InputCount(); ++j) {
      inputs_.push_back({i, j});
      keys_.push_back(tx.Input(j).previous_output);
    }
  }
  // Sort by keys, ready for query.
  SortTogether(keys_.begin(), keys_.end(), inputs_.begin());
  state_ = State::Parsed;
}

inline void SpendJoiner::Append() {
  Assert(state_ == State::Parsed);
  db_.Append(*block_, height_);
  state_ = State::Appended;
}

inline bool SpendJoiner::Query() {
  Assert(state_ == State::Appended);
  rids_.resize(keys_.size());
  // TODO: Change API so Query takes &rids_[0] for clarity
  const int found_count = db_.Query(keys_, rids_, height_);
  if (found_count != std::ssize(keys_)) {
    state_ = State::Error;
    return false;
  }
  keys_.clear();
  SortTogether(rids_.begin(), rids_.end(), inputs_.begin());
  state_ = State::Queried;
  return true;
}

// Fetch the output records from the outputs table.
inline bool SpendJoiner::Fetch() {
  if (state_ == State::Error) return false;
  Assert(state_ == State::Queried);
  std::tie(outputs_, scripts_) = db_.Fetch(rids_);

  // Sort by inputs_, ready for join.
  SortTogether(inputs_.begin(), inputs_.end(), outputs_.begin());
  rids_ .clear();
  state_ = outputs_.size() == inputs_.size() ? State::Fetched : State::Error;
  return state_ == State::Fetched;
}

// Join a range of inputs with its found output and call back with the merged view.
inline void SpendJoiner::Join() {
  Assert(state_ == State::Fetched);
  Assert(inputs_.size() == outputs_.size());

  ParallelFor(0, std::ssize(outputs_), [&](int index) {
    const OutputDetail& detail = outputs_[index];
    const OutputHeader& header = detail.header;
    const consensus::SpendRecord spend{ 
      .funding_height = header.height,
      .funding_flags = header.flags,
      .amount = header.amount,
      .pubkey_script = detail.script.Span(scripts_),
      .tx = block_->Transaction(inputs_[index].tx_index),
      .spend_input_index = inputs_[index].input_index
    };
    callback_(spend);
  });
  inputs_.clear();
  outputs_.clear();
  scripts_.clear();
  block_.reset();
  state_ = Joined;
}

inline bool SpendJoiner::Advance() {
  switch (state_) {
    case State::Init:     Parse();  break;
    case State::Parsed:   Append(); break;
    case State::Appended: Query();  break;
    case State::Queried:  Fetch();  break;
    case State::Fetched:  Join();   break;
    default: return false;
  }
  return state_ != State::Error;
}

}  // namespace hornet::data::utxo
