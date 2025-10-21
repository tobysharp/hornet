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
  enum class State { Init, Parsed, Appended, QueriedPartial, Queried, FetchedPartial, Fetched, Joined, Error };

  using Callback = std::function<consensus::Result(const consensus::SpendRecord&)>;

  SpendJoiner(Database& db, 
              std::shared_ptr<const protocol::Block> block, 
              int height/*,
              Callback callback*/) 
              : state_(State::Init), db_(db), block_(block), height_(height)/*, callback_(callback) */ {
    Parse();
  }

  explicit operator bool() const { return state_ != Error; }
  State GetState() const { return state_; }
  bool Advance();
  consensus::Result Join(auto&& callback);

 private:
  void Parse();
  void Append();
  bool Query();
  bool Fetch();
  //void Join();

  State state_;
  Database& db_;
  std::shared_ptr<const protocol::Block> block_;
  const int height_;
  //const Callback callback_;
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
  db_.Append(*block_, height_);  // TODO: Enable out-of-order appends
  state_ = State::Appended;
}

inline void SpendJoiner::Query() {
  Assert(state_ == State::Appended || state_ == State::QueriedPartial || state_ == State::FetchedPartial);
  rids_.resize(keys_.size());
  outputs_.resize(keys_.size());
  const int commit_height = db_.GetContiguousLength();
  const int query_since = query_before_;  // Initially zero.
  if (query_since >= commit_height) return;
  query_before_ = std::min(commit_height, height_);
  // TODO: Change API so Query takes &rids_[0] for clarity
  const auto found = db_.Query(keys_, rids_, query_since, query_before_);
  found_funded_ += found.funded;
  Assert(found_funded_ <= std::ssize(keys_));
  if (found.spent > 0) 
    return GotoError();  // One or more of the required UTXOs has in fact been spent.
  if (query_before_ < height_) {
    // The executed query was partial.
    state_ = State::QueriedPartial;
  } else {
    if (found_funded_ != std::ssize(keys_))
      return GotoError();  // Not all of the required UTXOs were found in the database.
    keys_.clear();
    SortTogether(rids_.begin(), rids_.end(), inputs_.begin());
    state_ = State::Queried;
  }
}

// Fetch the output records from the outputs table.
inline void SpendJoiner::Fetch() {
  Assert(state_ == State::Queried || state_ == State::QueriedPartial);

  fetch_count_ += db_.Fetch(rids_, outputs_, &scripts_);
  Assert(fetch_count_ == found_funded_);
  std::fill(rids_.begin(), rids_.end(), kNullOutputId);  // Prevent fetching again.

  if (state_ == State::QueriedPartial) {
    // We've done a partial fetch. Next action should be a residual query.
    state_ = State::FetchedPartial;
  } else {
    Assert(state_ == State::Queried);
    if (fetch_count_ != std::ssize(inputs_))
      return GotoError();  // Not all of the required UTXO data was fetched.
    // Sort by inputs_, ready for join.
    SortTogether(inputs_.begin(), inputs_.end(), outputs_.begin());
    rids_.clear();
    state_ = State::Fetched;
  }
}

// Join a range of inputs with its found output and call back with the merged view.
inline consensus::Result SpendJoiner::Join(auto&& callback) {
  Assert(state_ == State::Fetched);
  Assert(inputs_.size() == outputs_.size());

  consensus::Result rv = {};
  ParallelFor(0, std::ssize(outputs_), [&](int index) {
    if (!rv) return;
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
    if (const consensus::Result result = callback(spend); !result)
      rv = result;
  });
  inputs_.clear();
  outputs_.clear();
  scripts_.clear();
  block_.reset();
  state_ = Joined;
  return rv;
}

inline bool SpendJoiner::IsReady() const {
  switch (state_) {
    case State::Init:           
    case State::Parsed:         
    case State::Appended:       
    case State::QueriedPartial:  
    case State::Queried:        
      return true;
    case State::FetchedPartial:
      // We could permit small incremental queries, but we may prefer to wait until all data has arrived.
      // return query_before_ < db_.GetContiguousLength();
      return height_ <= db_.GetContiguousLength();
    default:
      return false;
  }
}

inline bool SpendJoiner::Advance() {
  switch (state_) {
    case State::Init:           Parse();  break;
    case State::Parsed:         Append(); break;
    case State::Appended:
    case State::FetchedPartial: Query();  break;
    case State::Queried:        Fetch();  break;
    case State::QueriedPartial: 
      if (db_.GetContiguousLength() >= height_) Query();
      else Fetch(); 
      break;
    default: return false;
  }
  return state_ != State::Error;
}

}  // namespace hornet::data::utxo
