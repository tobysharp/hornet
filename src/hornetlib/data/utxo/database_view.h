#pragma once

#include <memory>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/protocol/block.h"

namespace hornet::data::utxo {

class DatabaseView : public consensus::ChainOutputsView {
 public:
  DatabaseView(std::shared_ptr<SpendJoiner> ptr) : joiner_(std::move(ptr)) {}

  // [S01] Returns success iff no output in this block duplicates an unspent outpoint created by a preceding transaction
  // (BIP30).
  bool QueryOutPointsUnique(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    joiner_->WaitForDependencies();
    return joiner_->AllOutPointsUnique();
  }

  // [S02] Returns success iff every spending input in this block references an output created by a preceding transaction.
  bool QueryPreviousOutputsCreated(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    return joiner_->AllOutPointsCreated();
  }

  // [S03] Returns success iff no spending input in this block references an output spent by a preceding transaction.
  bool QueryPreviousOutputsUnspent(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    return joiner_->AllOutPointsUnspent();
  }

  std::optional<consensus::JoinedSpendRange> Spends(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    if (!joiner_->WaitForJoin()) return std::nullopt;
    return *joiner_;
  }

 private:
  std::shared_ptr<SpendJoiner> joiner_;
};

}  // namespace hornet::data::utxo
