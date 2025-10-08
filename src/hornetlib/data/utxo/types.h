#pragma once

#include <compare>
#include <cstdint>
#include <vector>

#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/subarray.h"

namespace hornet::data::utxo {

struct OutputHeader {
  int height;
  uint32_t flags;
  int64_t amount;
};

struct OutputDetail {
  OutputHeader header;
  util::SubArray<uint8_t> script;
};

struct Outputs {
  std::vector<OutputDetail> details;
  std::vector<uint8_t> scripts;
};

struct InputHeader {
  std::strong_ordering operator <=>(const InputHeader&) const = default;
  int tx_index;
  int input_index;
};

template <typename Key, typename Value>
struct KeyValue {
  inline friend std::strong_ordering operator<=>(const KeyValue& lhs, const Key& rhs) {
    return lhs.key <=> rhs;
  }
  inline friend std::strong_ordering operator<=>(const Key& lhs, const KeyValue& rhs) {
    return lhs <=> rhs.key;
  }

  Key key;
  Value value;
};

using OutputKey = protocol::OutPoint;
using OutputId = uint64_t;
using OutputKV = KeyValue<OutputKey, OutputId>;

}  // namespace hornet::data::utxo
