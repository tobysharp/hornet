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

  // Returns success if every spending input in this block references an existing unspent output.
  consensus::Result QueryPrevoutsUnspent(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    if (!joiner_->WaitForQuery()) 
      return consensus::Error::Spending_PrevoutNotUnspent;
    return {};
  }

  // Returns success if none of this block's transaction outputs already exist as unspent outputs (BIP30).
  consensus::Result QueryOutPointsUnique(const protocol::Block& block) const override {
    Assert(&block == joiner_->GetBlock().get());
    if (!joiner_->WaitForDependencies() || !joiner_->AllOutPointsUnique()) 
      return consensus::Error::Spending_DuplicateOutPoint;
    return {};
  }

 protected:
  consensus::Result EnumerateTransactions(const protocol::Block&, const Callback cb, const void* user) const override {
    if (!joiner_->WaitForFetch()) return consensus::Error::Spending_PrevoutNotUnspent;

    return joiner_->Join([&](const protocol::TransactionConstView& tx, std::span<const consensus::SpendRecord> spends) {
      return cb(tx, spends, user);
    });
  }

 private:
  std::shared_ptr<SpendJoiner> joiner_;
};

}  // namespace hornet::data::utxo
