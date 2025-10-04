#pragma once

#include <compare>
#include <cstdint>
#include <vector>

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

}  // namespace hornet::data::utxo
