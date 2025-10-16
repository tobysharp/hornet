#pragma once

#include <cstdint>
#include <span>

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class RecentStore {
 public:
  int Query(std::span<const protocol::OutPoint> prevouts,
            std::span<uint64_t> ids) const;

  int Fetch(uint64_t id, uint8_t* buffer, int buffer_size) const;
  
  void Append(const protocol::Block& block, int height);

  void RemoveSince(int height);

  int Size() const;

  // Returns [begin, end) heights.
  std::pair<int, int> GetHeightRange() const;
};

}  // namespace hornet::data::utxo
