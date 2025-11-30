#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
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
              int height) 
              : state_(State::Init), db_(db), block_(block), height_(height) {}

  explicit operator bool() const { return state_ != State::Error; }
  State GetState() const { return state_; }
  int GetHeight() const { return height_; }
  
  bool IsAdvanceReady() const;
  bool Advance();
  
  bool IsJoinReady() const { return state_ == State::Fetched; }
  consensus::Result Join(auto&& callback);

  bool WaitForQuery() const;
  bool WaitForFetch() const;

 private:
  void Parse();
  void Append();
  void Query();
  void Fetch();
  void GotoError();
  void ReleaseQuery();
  void ReleaseFetch();

  std::atomic<State> state_;
  std::atomic<bool> release_query_ = false;
  std::atomic<bool> release_fetch_ = false;

  Database& db_;
  std::shared_ptr<const protocol::Block> block_;
  const int height_;
  int query_before_ = 0;
  int found_funded_ = 0;
  int fetch_count_ = 0;
  std::vector<InputHeader> inputs_;
  std::vector<OutputKey> keys_;
  std::vector<OutputId> rids_;
  std::vector<OutputDetail> outputs_;
  std::vector<uint8_t> scripts_;
};

inline void SpendJoiner::Parse() {
  Assert(state_ == State::Init);
  for (int i = 0; i < block_->GetTransactionCount(); ++i) {
    const auto tx = block_->Transaction(i);
    for (int j = 0; j < tx.InputCount(); ++j) {
      const auto& prevout = tx.Input(j).previous_output;
      if (!prevout.IsNull()) {
        inputs_.push_back({i, j});
        keys_.push_back(tx.Input(j).previous_output);
      }
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
    // Note we only need to include outputs_ in this permutation if we have already done a Fetch.
    SortTogether(rids_.begin(), rids_.end(), inputs_.begin(), outputs_.begin());
    state_ = State::Queried;
    ReleaseQuery();
  }
}

// Fetch the output records from the outputs table.
inline void SpendJoiner::Fetch() {
  Assert(state_ == State::Queried || state_ == State::QueriedPartial);

  if (state_ == State::QueriedPartial) {
    // Since the previous action was a partial query, we haven't yet sorted the rid's.
    SortTogether(rids_.begin(), rids_.end(), inputs_.begin(), keys_.begin()/*, outputs_.begin() */);
    // We only need to include outputs_ in the permutation if this isn't the first fetch, which implies
    // partial query -> partial fetch -> 2nd partial query, a code path we don't currently support.
  }

  fetch_count_ += db_.Fetch(rids_, outputs_, &scripts_);
  Assert(fetch_count_ == found_funded_);

  if (state_ == State::QueriedPartial) {
    // We've done a partial fetch. Next action should be a residual query.
    state_ = State::FetchedPartial;
    SortTogether(keys_.begin(), keys_.end(), inputs_.begin(), rids_.begin(), outputs_.begin());
  } else {
    Assert(state_ == State::Queried);
    if (fetch_count_ != std::ssize(inputs_))
      return GotoError();  // Not all of the required UTXO data was fetched.
    // Sort by inputs_, ready for join.
    SortTogether(inputs_.begin(), inputs_.end(), outputs_.begin());
    rids_.clear();
    state_ = State::Fetched;
    ReleaseFetch();
  }
}

// Join a range of inputs with its found output and call back with the merged view.
inline consensus::Result SpendJoiner::Join(auto&& callback) {
  Assert(state_ == State::Fetched);
  Assert(inputs_.size() == outputs_.size());

  consensus::Result rv = {};
  std::atomic<bool> failed = false;
  ParallelFor<int>(0, std::ssize(outputs_), [&](int index) {
    if (failed) return;
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
    if (const consensus::Result result = callback(spend); !result) {
      bool expected = false;
      if (failed.compare_exchange_strong(expected, true)) rv = result;
    }
  });
  inputs_.clear();
  outputs_.clear();
  scripts_.clear();
  block_.reset();
  state_ = State::Joined;
  return rv;
}

inline bool SpendJoiner::IsAdvanceReady() const {
  switch (state_) {
    case State::Init:           
    case State::Parsed:         
    case State::Appended:       
    case State::QueriedPartial:  
    case State::Queried:        
      return true;
    case State::FetchedPartial:
      // We could permit small incremental queries, but we may prefer to wait until all residual data has arrived.
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


inline void SpendJoiner::ReleaseQuery() {
  release_query_ = true;
  release_query_.notify_all();
}

inline void SpendJoiner::ReleaseFetch() {
  release_fetch_ = true;
  release_fetch_.notify_all();
}

inline void SpendJoiner::GotoError() {
  state_ = State::Error;
  ReleaseQuery();
  ReleaseFetch();
}

inline bool SpendJoiner::WaitForQuery() const {
  release_query_.wait(false);
  return state_ != State::Error;
}

inline bool SpendJoiner::WaitForFetch() const {
  release_fetch_.wait(false);
  return state_ != State::Error;
}

}  // namespace hornet::data::utxo
