#pragma once

#include <span>
#include <vector>

#include "hornetlib/consensus/utxo.h"

namespace hornet::data::utxo {

struct OutputHeader {
  int height;
  uint32_t flags;
  int64_t amount;
};

class QueryResults {
 public:
  struct InputHeader {
    int spend_tx_index;
    int spend_tx_input;
  };
  QueryResults() = default;
  QueryResults(int prevouts, int script_bytes) { 
    join_.resize(prevouts);
    scripts_.reserve(script_bytes);
  }
  QueryResults(QueryResults&& rhs) = default;

  void Set(int query_index, const OutputHeader& output, std::span<const uint8_t> pubkey_script, const InputHeader& input) {
    auto start = scripts_.insert(scripts_.end(), pubkey_script.begin(), pubkey_script.end());
    join_[query_index] = {
     .output = output,
     .script = { static_cast<int>(start - scripts_.begin()), static_cast<int>(pubkey_script.size()) },
     .input = input
    };
  }

 protected:
  struct Entry {
    OutputHeader output;
    util::SubArray<uint8_t> script;
    InputHeader input;
  };
  std::vector<Entry> join_;
  std::vector<uint8_t> scripts_;
};

}  // namespace hornet::data::utxo
