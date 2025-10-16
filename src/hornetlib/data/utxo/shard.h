#pragma once

namespace hornet::data::utxo {

// A shard of the committed rows index.
class Shard {
 public:
  int Query(std::span<const protocol::OutPoint> prevouts, std::span<OutputId> ids) const;

 
};

}  // namespace hornet::data::utxo
