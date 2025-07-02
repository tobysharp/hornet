#pragma once

#include <cstdint>

#include "hornetlib/encoding/transfer.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/as_span.h"

namespace hornet::protocol {

enum class InventoryType : uint32_t {
  Error = 0,
  Transaction = 1,
  Block = 2,
  FilteredBlock = 3,
  CompactBlock = 4,
  WitnessTransaction = 0x40000001,
  WitnessBlock = 0x40000002,
  FilteredWitnessBlock = 0x40000003
};

struct Inventory {
  InventoryType type;
  Hash hash;

  static Inventory WitnessBlock(const Hash& hash) {
    return {InventoryType::WitnessBlock, hash};
  }

  template <typename Streamer, typename T>
  static void Transfer(Streamer& s, T& obj) {
    using namespace hornet::encoding;
    TransferEnumLE4(s, obj.type);
    TransferBytes(s, util::AsSpan(obj.hash));
  }
};

}  // namespace hornet::protocol
