#pragma once

#include <memory>

#include "hornetlib/consensus/utxo.h"
#include "hornetlib/data/utxo/index.h"
#include "hornetlib/data/utxo/outputs_table.h"

namespace hornet::data::utxo {

class DatabaseView : public consensus::UnspentTransactionsView {
 public:
   struct Info {};

  // Retrieve stats on the internal state of this object.
  Info GetInfo() const;

  // Mark input prevouts as spent and add new outputs.
  // May use cached mutable state from the previous EnumerateUnspentImpl to save duplicated work.
  void ConnectBlock(const protocol::Block& block);

  // Explicitly compact the representation, which may be an expensive operation.
  void Compact();
  
  consensus::Result QueryPrevoutsUnspent(const protocol::Block& block) const override;

 protected:
  consensus::Result EnumerateSpends(const protocol::Block& block, const Callback cb,
                                 const void* user) const override;

  OutputsTable table_;
  UnspentIndex index_;
};

}  // namespace hornet::data::utxo
