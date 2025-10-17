#pragma once

#include <span>
#include <memory>
#include <vector>

#include "hornetlib/data/utxo/compacter.h"
#include "hornetlib/data/utxo/memory_age.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

// A shard of the unspent outputs index.
class Shard {
 public:
  Shard(const std::vector<MemoryAge::Options>& age_options);

  int Query(std::span<const OutputKey> prevouts, std::span<OutputId> ids) const;
  // TODO: Other methods Append, EraseSince, etc.

 private:
  Compacter compacter_;
  std::vector<std::unique_ptr<MemoryAge>> ages_;
};

inline Shard::Shard(const std::vector<MemoryAge::Options>& age_options) {
  ages_.resize(age_options.size());
  ages_.back() = std::make_unique<MemoryAge>(age_options.back());
  for (int i = std::ssize(ages_) - 2; i >= 0; ++i) {
    MemoryAge* parent = ages_[i + 1].get();
    ages_[i] = std::make_unique<MemoryAge>(age_options[i], 
      [&, parent](MemoryAge* src) { compacter_.EnqueueMerge(src, parent); });
  }
}

}  // namespace hornet::data::utxo
