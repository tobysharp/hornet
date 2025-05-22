#pragma once

#include "encoding/reader.h"
#include "encoding/writer.h"
#include "encoding/transfer.h"
#include "protocol/constants.h"
#include "util/throw.h"

namespace hornet::protocol {

class BlockHeader {
 public:
  void Serialize(encoding::Writer& w) const {
    Transfer(w, *this);
  }
  void Deserialize(encoding::Reader& r) {
    Transfer(r, *this);
    if (txn_count_ != 0) util::ThrowOutOfRange("In block header, tx_count has nonzero value ", txn_count_, ".");
  }

 private:
  template <typename S, typename T>
  static void Transfer(S& s, T& t) {
    TransferLE4(s, t.version_);
    TransferBytes(s, util::AsSpan(t.prev_block_));
    TransferBytes(s, util::AsSpan(t.merkle_root_));
    TransferLE4(s, t.timestamp_);
    TransferLE4(s, t.bits_);
    TransferLE4(s, t.nonce_);
    TransferVarInt(s, t.txn_count_);
  } 
  
  int32_t version_;         // Block version information (note, this is signed).
  Hash prev_block_;         // The hash value of the previous block this particular block references.
  Hash merkle_root_;        // The ereference to a Merkle tree collection which is a hash of all transactions related to this block.
  uint32_t timestamp_;      // A timestamp recording when this block was created (Will overflow in 2106).
  uint32_t bits_;           // The calculated difficulty target being used for this block.
  uint32_t nonce_;          // The nonce used to generate this block... to allow variations of the header and compute different hashes.
  uint32_t txn_count_ = 0;  // Number of transaction entries, this value is always 0.
};

}  // namespace hornet::protocol
