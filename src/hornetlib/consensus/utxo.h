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
  protocol::TransactionConstView tx;
  int spend_input_index;
};

// This class represents an abstract view onto the whole set of unspent outputs.
class UnspentOutputsView {
 public:
  virtual ~UnspentOutputsView() = default;

  virtual Result QueryPrevoutsUnspent(const protocol::Block& block) const = 0;

  template <typename Fn>
  Result ForEachSpend(const protocol::Block& block, Fn&& fn) const {
    struct Wrapper {
      static Result Thunk(const SpendRecord& spend, const void* user) {
        const auto* f = static_cast<const Fn*>(user);
        return (*f)(spend);
      }
    };
    return EnumerateSpends(block, &Wrapper::Thunk, &fn);
  }

 protected:
  using Callback = Result (*)(const SpendRecord&, const void* user);
  virtual Result EnumerateSpends(const protocol::Block& block, const Callback cb,
                                 const void* user) const = 0;
};

}  // namespace hornet::consensus
