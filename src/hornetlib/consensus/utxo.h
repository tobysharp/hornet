#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "hornetlib/consensus/types.h"
#include "hornetlib/protocol/block.h"

namespace hornet::consensus {

struct UnspentDetail {
  const int height;
  const uint32_t flags;
  const int64_t amount;
  const std::span<const uint8_t> script;
};

class UnspentTransactionsView {
 public:
  virtual ~UnspentTransactionsView() = default;

  template <typename Fn>
  Result ForEachUnspentPrevout(const protocol::Block& block, Fn&& fn) const {
    struct Wrapper {
      static Result Thunk(const int tx_index, const int input_index,
                                            const UnspentDetail& entry, const void* user) {
        const auto* f = static_cast<const Fn*>(user);
        return (*f)(tx_index, input_index, entry);
      }
    };
    return EnumerateUnspentImpl(block, &Wrapper::Thunk, &fn);
  }

 protected:
  using Callback = Result (*)(const int tx_index, const int input_index,
                                                const UnspentDetail& entry, const void* user);
  virtual Result EnumerateUnspentImpl(const protocol::Block& block,
                                      const Callback cb,
                                      const void* user) const = 0;
};

}  // namespace hornet::consensus
