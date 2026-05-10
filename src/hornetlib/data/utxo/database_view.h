#pragma once

#include <memory>

#include "hornetlib/consensus/types.h"
#include "hornetlib/consensus/utxo.h"
#include "hornetlib/data/utxo/joiner.h"
#include "hornetlib/protocol/block.h"

namespace hornet::data::utxo {

class DatabaseView : public consensus::UnspentOutputsView {
 public:
  DatabaseView(std::shared_ptr<SpendJoiner> ptr) : joiner_(std::move(ptr)) {}

  // Returns success iff every spending input in this block references an output created by a preceding transaction.
  bool QueryOutPointsCreated(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    return joiner_->AllOutPointsCreated();
  }

  // Returns success iff no spending input in this block references an output spent by a preceding transaction.
  bool QueryOutPointsUnspent(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    return joiner_->AllOutPointsUnspent();
  }

  // Returns success iff no output in this block duplicates an unspent outpoint created by a preceding transaction
  // (BIP30).
  bool QueryOutPointsUnique(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    joiner_->WaitForDependencies();
    return joiner_->AllOutPointsUnique();
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
