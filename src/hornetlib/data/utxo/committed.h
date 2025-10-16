#pragma once

#include <cstdint>
#include <span>

#include "hornetlib/data/utxo/io.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class CommittedStore {
 public:
  int Query(std::span<const protocol::OutPoint> prevouts,
            std::span<uint64_t> ids) const;

  uint64_t Size() const;

  IORequest MakeIORequest(uint64_t id, uint8_t* buffer, int buffer_size) const;
};

}  // namespace hornet::data::utxo
