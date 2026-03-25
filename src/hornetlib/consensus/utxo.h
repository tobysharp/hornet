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

struct JoinedSpend {
  protocol::TransactionConstView tx;
  std::span<const consensus::SpendRecord> spends;
};

class JoinedSpendRange {
 public:
  // Create a range object that forwards Size/At calls to the object `self`.
  template <typename T> JoinedSpendRange(const T& self) : self_(&self), vtable_(&kVTable<T>) {}

  int Size() const { return vtable_->Size(self_); }
  JoinedSpend operator[](int index) const { return vtable_->At(self_, index); }

  struct Sentinel{};
  struct Iterator {
    Iterator(const JoinedSpendRange& range, int index) : range_(range), index_(index) {}
    bool operator!=(Sentinel) const { return index_ < range_.Size(); }
    Iterator& operator++() {
      ++index_;
      return *this;
    }
    JoinedSpend operator*() const { return range_[index_]; }
   private:
    const JoinedSpendRange& range_;
    int index_;
  };

  Iterator begin() const { return {*this, 0}; }
  Sentinel end() const { return {}; }

 private:
  struct VTable {
    int (*Size)(const void* self);
    JoinedSpend (*At)(const void* self, int index);
  };
  template <typename T> static const T* Cast(const void* self) { return static_cast<const T*>(self); }
  template <typename T> static inline const VTable kVTable = {
    [](const void* self) -> int { return Cast<T>(self)->SpendSize(); },
    [](const void* self, int index) -> JoinedSpend { return Cast<T>(self)->SpendAt(index); }
  };
  const void* self_ = nullptr;
  const VTable* vtable_ = nullptr;
};

// This class represents an abstract view onto the whole set of unspent outputs.
class UnspentOutputsView {
 public:
  virtual ~UnspentOutputsView() = default;

  // Returns success if every spending input in this block references an existing unspent output.  
  virtual Result QueryPrevoutsUnspent(const protocol::Block& block) const = 0;

  // Returns success if none of this block's transaction outputs already exist as unspent outputs (BIP30).
  virtual Result QueryOutPointsUnique(const protocol::Block& block) const = 0;

  virtual std::expected<JoinedSpendRange, Error> Spends(const protocol::Block& block) const = 0;

  template <typename Fn>
  Result ForEachTransaction(const protocol::Block& block, Fn&& fn) const {
    const auto spends = Spends(block); 
    if (!spends) return spends.error();
    for (JoinedSpend spend : *spends) {
      if (const consensus::Result result = std::invoke(fn, spend.tx, spend.spends); !result) return result;
    }
    return {};
  }

  template <typename Fn>
  auto SumTransactions(const protocol::Block& block, Fn&& fn) const 
    -> std::expected<std::remove_cvref_t<std::invoke_result_t<Fn&, const protocol::TransactionConstView&, const std::span<const SpendRecord>&>>, Error>{
    using T = std::remove_cvref_t<std::invoke_result_t<Fn&, const protocol::TransactionConstView&, const std::span<const SpendRecord>&>>;
    T sum{};
    const auto spends = Spends(block); 
    if (!spends) return std::unexpected{spends.error()};
    for (JoinedSpend spend : *spends) sum += std::invoke(fn, spend.tx, spend.spends);
    return sum;
  }
};

}  // namespace hornet::consensus
