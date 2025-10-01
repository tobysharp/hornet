#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/search.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

namespace hornet::data::utxo {

// Returns bits of the hash that can be compared numerically the same way that bytes are compared
// lexicographically. The most significant `skip_bits` bits are discarded, and the next most 
// significant `keep_bits` are returned, shifted into the lowest order bits.
// Caller must guarantee (skip_bits + keep_bits <= 64).
// In practice skip_bits~22, keep_bits~10 so this is no problem.
inline uint64_t GetLexicographicWord(const protocol::Hash& hash, int skip_bits, int keep_bits) {
  uint64_t le_word;
  std::memcpy(&le_word, hash.data(), sizeof(le_word));
  const uint64_t be_word = __builtin_bswap64(le_word);
  const uint64_t stripped = be_word << skip_bits;
  return stripped >> (64 - keep_bits);
}

class Codec {
 public:
  using KeyType = uint16_t;
  using ValueType = uint16_t;
  struct KeyValueType { uint16_t kv; };
  class StrongComparator;

  Codec(int value_bits)
      : key_bits_((sizeof(KeyValueType) << 3) - value_bits),
        value_bits_(value_bits),
        key_mask_(static_cast<KeyType>((1 << key_bits_) - 1)),
        value_mask_(static_cast<ValueType>((1 << value_bits_) - 1)) {}

  KeyType Key(KeyValueType kv) const {
    return static_cast<KeyType>((kv.kv >> value_bits_) & key_mask_);
  }

  ValueType Value(KeyValueType kv) const {
    return static_cast<ValueType>(kv.kv & value_mask_);
  }

  KeyValueType Encode(KeyType key, ValueType value) const {
    const ValueType clamped = std::min(value, value_mask_);
    return static_cast<KeyValueType>(((key & key_mask_) << value_bits_) | clamped);
  }

  int KeyBits() const { return key_bits_; }
  int ValueBits() const { return value_bits_; }

  StrongComparator Strong() const;

 private:
  const int key_bits_;
  const int value_bits_;
  const KeyType key_mask_;
  const ValueType value_mask_;
};

class Codec::StrongComparator {
 public:
  StrongComparator(const Codec& codec) : codec_(codec) {}
  std::strong_ordering operator()(const Codec::KeyValueType lhs, const Codec::KeyType rhs) const {
    return codec_.Key(lhs) <=> rhs;
  }

  private:
  const Codec& codec_;
};

inline Codec::StrongComparator Codec::Strong() const {
  return *this;
}

class CompactIndex {
 public:
  CompactIndex(int skip_bits, int value_bits) : codec_(value_bits), skip_bits_(skip_bits) {}

  // Queries the span of `OutPoint`s `queries` for candidate matches in the compact index
  // within the range of elements [lo, hi). Calls `visit` for each candidate match, like
  // visit(match_index, query_index, value). For each candidate, caller is guaranteed
  // that value <= exact_run_index if the index contains an exact match. False positives
  // are also possible, i.e. candidate matches where no exact match exists.
  template <typename Visit>
  int Query(std::span<const protocol::OutPoint> queries, const int lo, const int hi,
            Visit&& visit) const {
    const auto key = [&](const protocol::OutPoint& op) { 
      return KeyPrefix(op);
    };
    const auto callback = [&](int match_index, int query_index, Codec::KeyValueType kv) {
      return visit(match_index, query_index, codec_.Value(kv));
    };
    const auto lower = compact_.begin() + std::max(lo, 0);
    const auto upper = hi < std::ssize(compact_) ? compact_.begin() + hi : compact_.end();
    return ForEachMatchInDoubleSorted(queries.begin(), queries.end(), lower, upper, key, 
                                      codec_.Strong(), callback);
  }

  Codec::KeyType KeyPrefix(const protocol::OutPoint& query) const {
    return static_cast<Codec::KeyType>(GetLexicographicWord(query.hash, skip_bits_, codec_.KeyBits())); 
  }

 private:
  Codec codec_;
  const int skip_bits_;
  std::vector<Codec::KeyValueType> compact_;
};

}  // namespace hornet::data::utxo
