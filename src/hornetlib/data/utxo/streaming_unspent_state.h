#pragma once

#include <memory>

#include "hornetlib/consensus/utxo.h"

namespace hornet::data::utxo {

// UnspentTransactionsView backend, optimized for initial block download.
// No explicit rewind/reorg support (but can be added later if needed).
class StreamingUnspentState : public consensus::UnspentTransactionsView {
 public:
 protected:
  class Impl;

  virtual consensus::Result EnumerateUnspentImpl(const protocol::Block& block,
                                      const Callback cb,
                                      const void* user) const override;

  std::shared_ptr<Impl> impl_;                                      
};

}  // namespace hornet::data::utxo
