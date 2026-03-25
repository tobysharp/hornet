#pragma once

#include <atomic>
#include <cstdint>
#include <ranges>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/profiler.h"
#include "hornetlib/data/utxo/sort.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/metrics.h"
#include "hornetlib/util/subarray.h"

namespace hornet::data::utxo {

class SpendJoiner {
 public:
  enum class State { Init, Parsed, Appended, QueriedPart, Queried, FetchedPart, Fetched, Joined, Error, Cancelled };

  using Callback = std::function<consensus::Result(const consensus::SpendRecord&)>;

  struct CancelledException : public std::exception {
    const char* what() const noexcept override { return "SpendJoiner cancelled."; }
  };

  SpendJoiner(Database& db, std::shared_ptr<const protocol::Block> block, int height, bool disable_fetch = false)
      : state_(State::Init), db_(db), block_(block), height_(height), disable_fetch_(disable_fetch) {}

  State GetState() const { return state_; }
  int GetHeight() const { return height_; }
  const std::shared_ptr<const protocol::Block>& GetBlock() const { return block_; }

  bool IsAdvanceReady() const;

  enum class Action { Advance, Wait, Consume, Retire };
  struct StepResult {
    State state;
    Action action;
  };
  StepResult Advance();

  bool IsJoinReady() const { return state_ == State::Joined; }
  bool IsAssumeValid() const { return disable_fetch_; }
  consensus::Result Join(auto&& callback);
  bool AllOutPointsUnique() const;

  bool WaitForQuery() const;
  bool WaitForJoin() const;
  bool WaitForDependencies() const;

  void Cancel();

  int SpendSize() const { return std::ssize(joined_); }
  consensus::JoinedSpend SpendAt(int index) const {
    return {block_->Transaction(joined_[index].tx_index), joined_[index].spending.Span(spends_)};
  }

  enum Timer { Time_Parse, Time_Append, Time_Query, Time_Fetch, Time_Join, Time_Count };
  using Metrics = util::Metrics<Time_Count>;
  const Metrics& GetMetrics() const { return metrics_; }

 private:
  struct JoinedSpendStorage {
    int tx_index;
    util::SubArray<consensus::SpendRecord> spending;
  };

  void Parse();
  void Append();
  void Query();
  void Fetch();
  void Join();
  void GotoError();
  void ReleaseQuery();
  void ReleaseJoin();

  std::atomic<State> state_;
  std::atomic<bool> release_query_ = false;
  std::atomic<bool> release_join_ = false;

  Database& db_;
  std::shared_ptr<const protocol::Block> block_;
  const int height_;
  const bool disable_fetch_;
  int query_before_ = 0;
  int found_funded_ = 0;
  int fetch_count_ = 0;
  std::optional<Database::Pin> pin_;
  std::vector<InputHeader> inputs_;
  std::vector<OutputKey> keys_;
  std::vector<OutputId> rids_;
  std::vector<OutputDetail> outputs_;
  std::vector<uint8_t> scripts_;
  std::vector<consensus::SpendRecord> spends_;
  std::vector<JoinedSpendStorage> joined_;
  mutable Metrics metrics_;
};

inline void SpendJoiner::Parse() {
  Assert(state_ == State::Init);

  const auto timer = metrics_.AddScoped(Time_Parse);
  ScopedProfiler profiler("Parse", height_);

  int size = 0;
  for (const auto& tx : block_->Transactions())
    for (const auto& input : tx.Inputs()) size += !input.previous_output.IsNull();

  inputs_.resize(size);
  keys_.resize(size);
  rids_.resize(size);
  outputs_.resize(size);

  const int local_count = Database::ParseBlockPrevouts(*block_, height_, inputs_, keys_, rids_, outputs_, scripts_);
  if (local_count < 0) return GotoError();
  found_funded_ = fetch_count_ = local_count;

  // Sort by keys, ready for query.
  SortTogether(keys_.begin(), keys_.end(), inputs_.begin(), rids_.begin(), outputs_.begin());

  // If any keys are duplicates, that implies that the block is trying to spend the same prevout twice.
  if (std::adjacent_find(keys_.begin(), keys_.end()) != keys_.end()) return GotoError();

  state_ = State::Parsed;
}

inline void SpendJoiner::Append() {
  Assert(state_ == State::Parsed);
  const auto timer = metrics_.AddScoped(Time_Append);
  ScopedProfiler profiler("Append", height_);
  pin_ = db_.Append(*block_, height_);
  state_ = State::Appended;
}

inline void SpendJoiner::Query() {
  Assert(state_ == State::Appended || state_ == State::QueriedPart || state_ == State::FetchedPart);

  const auto timer = metrics_.AddScoped(Time_Query);
  ScopedProfiler profiler("Query", height_);

  const int commit_height = db_.GetContiguousLength();
  const int query_since = query_before_;  // Initially zero.
  if (query_since >= commit_height) return;
  query_before_ = std::min(commit_height, height_);
  // TODO: Change API so Query takes &rids_[0] for clarity
  const auto found = db_.Query(keys_, rids_, query_since, query_before_);
  found_funded_ += found.funded;
  Assert(found_funded_ <= std::ssize(keys_));
  if (found.spent > 0) return GotoError();  // One or more of the required UTXOs has in fact been spent.
  if (query_before_ < height_) {
    // The executed query was partial.
    state_ = State::QueriedPart;
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
  Assert(state_ == State::Queried || state_ == State::QueriedPart);
  Assert(!disable_fetch_);

  const auto timer = metrics_.AddScoped(Time_Fetch);
  ScopedProfiler profiler("Fetch", height_);

  if (state_ == State::QueriedPart) {
    // Since the previous action was a partial query, we haven't yet sorted the rid's.
    SortTogether(rids_.begin(), rids_.end(), inputs_.begin(), keys_.begin(), outputs_.begin());
  }

  fetch_count_ += db_.Fetch(rids_, outputs_, &scripts_);
  Assert(fetch_count_ == found_funded_);

  if (state_ == State::QueriedPart) {
    // We've done a partial fetch. Next action should be a residual query.
    state_ = State::FetchedPart;
    SortTogether(keys_.begin(), keys_.end(), inputs_.begin(), rids_.begin(), outputs_.begin());
  } else {
    Assert(state_ == State::Queried);
    if (fetch_count_ != std::ssize(inputs_)) return GotoError();  // Not all of the required UTXO data was fetched.
    // Sort by inputs_, ready for join.
    SortTogether(inputs_.begin(), inputs_.end(), outputs_.begin());
    rids_.clear();
    state_ = State::Fetched;
  }
}

inline void SpendJoiner::Join() {
  Assert(state_ == State::Fetched);
  Assert(inputs_.size() == outputs_.size());

  const auto timer = metrics_.AddScoped(Time_Join);
  const int tx_count = block_->GetTransactionCount();

  spends_.resize(inputs_.size());
  joined_.clear();

  int tx_spend_index = 0;
  for (int i = 0; i < tx_count; ++i) {
    int spend_index = tx_spend_index;
    for (const auto& input : block_->Transaction(i).Inputs()) {
      if (input.previous_output.IsNull()) continue;
      const OutputDetail& output = outputs_[spend_index];
      spends_[spend_index] = {.funding_height = output.header.height,
                .funding_flags = output.header.flags,
                .amount = output.header.amount,
                .pubkey_script = output.script.Span(scripts_),
                .spend_input_index = inputs_[spend_index].input_index};
      spend_index++;
    }
    if (spend_index > tx_spend_index) joined_.push_back({i, {tx_spend_index, spend_index - tx_spend_index}});
    tx_spend_index = spend_index;
  }

  state_ = State::Joined;
  ReleaseJoin();
}

// Join a range of inputs with its found output and call back with the merged view.
inline consensus::Result SpendJoiner::Join(auto&& callback) {
  Assert(state_ == State::Joined);

  for (int i = 0; i < SpendSize(); ++i) {
    const auto spend = SpendAt(i);
    if (const consensus::Result result = callback(spend.tx, spend.spends); !result) return result;
  }
  return {};
}

inline bool SpendJoiner::IsAdvanceReady() const {
  switch (state_) {
    case State::Init:
    case State::Parsed:
    case State::Appended:
    case State::Fetched:
      return true;
    case State::QueriedPart:
      return !disable_fetch_ || height_ <= db_.GetContiguousLength();
    case State::Queried:
      return !disable_fetch_;
    case State::FetchedPart:
      // We could permit small incremental queries, but we may prefer to wait until all residual
      // data has arrived. return query_before_ < db_.GetContiguousLength();
      return height_ <= db_.GetContiguousLength();
    default:
      return false;
  }
}

inline SpendJoiner::StepResult SpendJoiner::Advance() {
  switch (state_) {
    case State::Init:
      Parse();
      break;
    case State::Parsed:
      Append();
      break;
    case State::Appended:
    case State::FetchedPart:
      Query();
      break;
    case State::Queried:
      if (!disable_fetch_) Fetch();
      break;
    case State::QueriedPart:
      if (db_.GetContiguousLength() >= height_) Query();
      else if (!disable_fetch_) Fetch();
      break;
    case State::Fetched:
      Join();
      break;
    default:
      break;
  }

  if (IsAdvanceReady()) return {state_, Action::Advance};
  if (state_ == State::FetchedPart || (state_ == State::QueriedPart && disable_fetch_)) return {state_, Action::Wait};
  if (state_ == State::Joined) return {state_, Action::Consume};

  return {state_, Action::Retire};
}

inline void SpendJoiner::ReleaseQuery() {
  release_query_ = true;
  release_query_.notify_all();
}

inline void SpendJoiner::ReleaseJoin() {
  release_join_ = true;
  release_join_.notify_all();
}

inline void SpendJoiner::GotoError() {
  state_ = State::Error;
  ReleaseQuery();
  ReleaseJoin();
}

inline bool SpendJoiner::WaitForQuery() const {
  release_query_.wait(false);
  if (state_ == State::Cancelled) throw CancelledException{};
  return state_ != State::Error;
}

// Blocks until all previous blocks have been appended to the database, ready to call AllOutPointsUnique.
inline bool SpendJoiner::WaitForDependencies() const {
  // NOTE: In practice, we're not specifically waiting for the dependencies, but rather for the Query
  // action to complete, which in turn implies the dependencies are present. This is a practical simplification.
  release_query_.wait(false);
  if (state_ == State::Cancelled) throw CancelledException{};
  return db_.GetContiguousLength() >= height_;
}

inline bool SpendJoiner::WaitForJoin() const {
  Assert(!disable_fetch_);
  release_join_.wait(false);
  if (state_ == State::Cancelled) throw CancelledException{};
  return state_ != State::Error;
}

inline void SpendJoiner::Cancel() {
  state_ = State::Cancelled;
  ReleaseQuery();
  ReleaseJoin();
}

inline bool SpendJoiner::AllOutPointsUnique() const {
  // Ensure that dependencies are in place before calling this method, e.g. by calling WaitForDependencies.
  Assert(db_.GetContiguousLength() >= height_);
  const auto timer = metrics_.AddScoped(Time_Query);

  // Count all the outputs.
  int outputs = 0;
  for (const auto& tx : block_->Transactions()) outputs += tx.OutputCount();

  // Create the output id keys and sort them.
  std::vector<protocol::OutPoint> keys(outputs);
  auto it = keys.begin();
  for (const auto& tx : block_->Transactions()) {
    const auto& txid = tx.GetHash();
    for (int i = 0; i < tx.OutputCount(); ++i) *it++ = {txid, static_cast<uint32_t>(i)};
  }
  std::ranges::sort(keys);

  // Perform the query.
  std::vector<OutputId> rids(keys.size());
  const auto found = db_.Query(keys, rids, height_);
  return found == 0;
}

}  // namespace hornet::data::utxo
