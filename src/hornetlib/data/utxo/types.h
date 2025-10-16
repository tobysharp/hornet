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

using OutputKey = protocol::OutPoint;
using OutputId = uint64_t;

struct OutputKV {
  inline friend std::strong_ordering operator<=>(const OutputKV& lhs, const OutputKey& rhs) {
    return lhs.key <=> rhs;
  }
  inline friend std::strong_ordering operator<=>(const OutputKey& lhs, const OutputKV& rhs) {
    return lhs <=> rhs.key;
  }

  OutputKey key;
  int height;
  OutputId rid;
};
static_assert(sizeof(OutputKV) == 48);

}  // namespace hornet::data::utxo
