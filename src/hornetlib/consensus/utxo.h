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
  JoinedSpendRange() {}
  // Create a range object that forwards Size/At calls to the object `self`.
  template <typename T> JoinedSpendRange(const T& self) : self_(&self), vtable_(&kVTable<T>) {}

  int Size() const { return self_ != nullptr ? vtable_->Size(self_) : 0; }
  JoinedSpend operator[](int index) const { Assert(self_ != nullptr); return vtable_->At(self_, index); }

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

  virtual std::optional<JoinedSpendRange> Spends(const protocol::Block& block) const = 0;
};

}  // namespace hornet::consensus
