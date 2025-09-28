#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/search.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/throw.h"

namespace hornet::data {

class CompactIndex {
 public:
  CompactIndex(int skip_bits) : skip_bits_(skip_bits) {}

  int Query(std::span<const protocol::OutPoint> queries,
                      uint32_t* candidates, const int size, const int lo = 0,
                      const int hi = std::numeric_limits<int>::max()) const {
    // We actually require that the size of the candidates buffer is sufficient to hold one 
    // candidate per query.
    if (size < static_cast<int>(queries.size()))
      util::ThrowInvalidArgument("CompactIndex::Query size of candidates fewer than queries.");

    const auto matcher = [&](const protocol::OutPoint& op) -> uint16_t {
      return KeyPrefix(op);
    };
    const auto visit = [&](int candidate_index, const CompactKeyValue& kv)  {
      candidates[candidate_index] = kv.Value();
    };
    const auto lower = compact_.begin() + std::max(lo, 0);
    const auto upper = hi < std::ssize(compact_) ? compact_.begin() + hi : compact_.end();
    return ForEachMatchInDoubleSorted(queries.begin(), queries.end(), lower, upper, matcher, visit);
  }

  uint16_t KeyPrefix(const protocol::OutPoint& out_point) const {
    constexpr int kTxidBits = 12;
    constexpr int kVoutBits = 4;

    uint32_t word;
    std::memcpy(&word, out_point.hash.data(), sizeof(word));

    uint32_t after = word >> skip_bits_;
    uint16_t txid_part = after & ((1 << kTxidBits) - 1);
    uint16_t vout_part = out_point.index & ((1 << kVoutBits) - 1);
    return (txid_part << kVoutBits) | vout_part;
  }

 private:
  class CompactKeyValue {
   public:
    CompactKeyValue(uint16_t key, uint32_t value)
        : storage_((key << kKeyBits) + (value & kValueMask)) {}
    CompactKeyValue(uint32_t storage) : storage_(storage) {}

    uint16_t Key() const {
      return static_cast<uint16_t>(storage_ >> kValueBits);
    }
    uint32_t Value() const {
      return storage_ & kValueMask;
    }
    uint32_t Storage() const {
      return storage_;
    }
    friend std::strong_ordering operator<=>(CompactKeyValue lhs, uint16_t rhs) {
      return lhs.Key() <=> rhs;
    }
    friend std::strong_ordering operator<=>(uint16_t lhs, CompactKeyValue rhs) {
      return lhs <=> rhs.Key();
    }
    static uint16_t MaximumKey() {
      return static_cast<uint16_t>((1 << kKeyBits) - 1);
    }
    static uint32_t MaximumValue() {
      return (1 << kValueBits) - 1;
    }

   private:
    static constexpr int kKeyBits = 13;
    static constexpr int kValueBits = 19;
    static constexpr int kValueMask = (1 << kValueBits) - 1;
    uint32_t storage_;
  };

  const int skip_bits_;
  std::vector<CompactKeyValue> compact_;
};

}  // namespace hornet::data
