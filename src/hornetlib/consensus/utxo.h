#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::consensus {

struct SpendRecord {
  int funding_height;
  uint32_t funding_flags;
  int64_t amount;
  std::span<const uint8_t> pubkey_script;
  int spend_input_index;

  bool IsCoinbase() const { return funding_flags & 1; }
};

// This class represents an abstract view onto the whole set of unspent outputs.
class UnspentOutputsView {
 public:
  virtual ~UnspentOutputsView() = default;

  // Returns success if every spending input in this block references an existing unspent output.  
  virtual Result QueryPrevoutsUnspent(const protocol::Block& block) const = 0;

  // Returns success if none of this block's transaction outputs already exist as unspent outputs (BIP30).
  virtual Result QueryOutPointsUnique(const protocol::Block& block) const = 0;

  template <typename Fn>
  Result ForEachTransaction(const protocol::Block& block, Fn&& fn) const {
    struct Wrapper {
      static Result Thunk(const protocol::TransactionConstView& tx,
                          std::span<const SpendRecord> spends, const void* user) {
        const auto* f = static_cast<const Fn*>(user);
        return (*f)(tx, spends);
      }
    };
    return EnumerateTransactions(block, &Wrapper::Thunk, &fn);
  }

 protected:
  using Callback = Result (*)(const protocol::TransactionConstView& tx,
                              std::span<const SpendRecord> spend, const void* user);
  virtual Result EnumerateTransactions(const protocol::Block& block, const Callback cb,
                                       const void* user) const = 0;
};

}  // namespace hornet::consensus
