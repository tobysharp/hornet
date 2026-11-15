#pragma once

#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "hornetlib/consensus/rules/validate_spending.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/utxo/database.h"
#include "hornetlib/data/utxo/database_view.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/model/header_context.h"

#include "testutil/blockchain.h"
#include "testutil/temp_folder.h"

namespace hornet::test {

void FixMerkleRoot(protocol::Block& block);

Blockchain LoadValidationPipelineChain();

template <typename Callback>
consensus::Result WithCandidateSpendState(const Blockchain& chain, const protocol::Block& block, Callback&& callback) {
  data::HeaderTimechain headers;
  TempFolder datadir;
  data::utxo::Database db{datadir.Path()};
  model::HeaderContext context;
  data::HeaderTimechain::ConstIterator tip;

  for (int height = 0; height < chain.Length(); ++height) {
    tip = headers.Add(context = context.Extend(chain[height]->Header())).it;
    if (height > 0) db.Append(*chain[height], height);
  }

  const int height = chain.Length();
  const auto ancestry = headers.GetValidationView(tip);
  auto joiner = std::make_shared<data::utxo::SpendJoiner>(db, std::make_shared<const protocol::Block>(block), height);

  while (joiner->IsAdvanceReady()) joiner->Advance();
  if (!joiner->IsJoinReady()) {
    const data::utxo::DatabaseView utxo{joiner};
    if (!utxo.QueryPreviousOutputsCreated(*joiner->GetBlock())) return consensus::Error::Spending_OutPointNotCreated;
    if (!utxo.QueryPreviousOutputsUnspent(*joiner->GetBlock())) return consensus::Error::Spending_OutPointSpent;
    return consensus::Error::Spending_OutPointSpent;
  }

  return std::forward<Callback>(callback)(*ancestry, joiner, height);
}

class StaticChainOutputsView : public consensus::ChainOutputsView {
 public:
  struct Entry {
    protocol::Transaction tx;
    std::vector<consensus::SpendRecord> spends;
  };

  void Add(protocol::Transaction tx, std::vector<consensus::SpendRecord> spends) {
    entries_.push_back({std::move(tx), std::move(spends)});
  }

  bool QueryPreviousOutputsCreated(const protocol::Block&) const override { return true; }
  bool QueryPreviousOutputsUnspent(const protocol::Block&) const override { return true; }
  bool QueryOutPointsUnique(const protocol::Block&) const override { return true; }

  std::optional<consensus::JoinedSpendRange> Spends(const protocol::Block&) const override {
    return *this;
  }

  int SpendSize() const { return std::ssize(entries_); }

  consensus::JoinedSpend SpendAt(int index) const {
    const auto& entry = entries_[index];
    return {entry.tx, std::span<const consensus::SpendRecord>{entry.spends}};
  }

 private:
  std::vector<Entry> entries_;
};

class NullSpendsChainOutputsView : public consensus::ChainOutputsView {
 public:
  bool QueryPreviousOutputsCreated(const protocol::Block&) const override { return true; }
  bool QueryPreviousOutputsUnspent(const protocol::Block&) const override { return true; }
  bool QueryOutPointsUnique(const protocol::Block&) const override { return true; }

  std::optional<consensus::JoinedSpendRange> Spends(const protocol::Block&) const override {
    return std::nullopt;
  }
};

}  // namespace hornet::test