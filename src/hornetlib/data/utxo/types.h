#pragma once

#include <compare>
#include <cstdint>
#include <vector>

#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/subarray.h"

// #define UTXO_LOG 1

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

static constexpr OutputId kNullOutputId = 0;
static constexpr OutputId kSpentOutputId = -1;

struct OutputKV {
  enum Operation { Delete = -1, Add = 0 };
  
  std::strong_ordering operator <=>(const OutputKV& rhs) const noexcept {
    if (auto cmp = key <=> rhs.key; cmp != 0) return cmp;
    return rhs.data.height <=> data.height;
  }
  bool operator ==(const OutputKV& rhs) const {
    return operator <=>(rhs) == 0;
  }
  bool operator !=(const OutputKV& rhs) const {
    return operator <=>(rhs) != 0;
  }
  friend std::strong_ordering operator<=>(const OutputKV& lhs, const OutputKey& rhs) {
    return lhs.key <=> rhs;
  }
  friend std::strong_ordering operator<=>(const OutputKey& lhs, const OutputKV& rhs) {
    return lhs <=> rhs.key;
  }
  bool IsAdd() const { return data.op == Operation::Add; }
  bool IsDelete() const { return data.op == Operation::Delete; }
  int Height() const { return data.height; }

  static OutputKV Spent(const OutputKey& key, int height) {
    return { key, { height, Delete }, kNullOutputId };
  }

  static OutputKV Funded(const OutputKey& key, int height, OutputId rid) {
    return { key, { height, Add }, rid };
  }

  OutputKey key;
  struct {
    int height : 31;
    int op     :  1;
  } data;
  OutputId rid;
};
static_assert(sizeof(OutputKV) == 48);

struct QueryResult {
  int funded = 0;
  int spent = 0;
  QueryResult operator +(const QueryResult& rhs) const { 
    return { funded + rhs.funded, spent + rhs.spent };
  }
};

}  // namespace hornet::data::utxo
