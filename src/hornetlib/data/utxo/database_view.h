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

  consensus::Result QueryPrevoutsUnspent(const protocol::Block&) const override {
    if (!joiner_->WaitForState(SpendJoiner::State::Queried)) 
      return consensus::Error::Transaction_NotUnspent;
    return {};
  }

 protected:
  Result EnumerateSpends(const protocol::Block&, const Callback cb,
                         const void* user) const override {
    if (!joiner_->WaitForState(SpendJoiner::State::Fetched))
      return consensus::Error::Transaction_NotUnspent;
    
    return joiner_->Join([&](const consensus::SpendRecord& spend) { 
      return cb(spend, user); 
    });
  }
    
 private:
  std::shared_ptr<SpendJoiner> joiner_;
};

}  // namespace hornet::data::utxo
