#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/codec.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class BlockOutputs {
 public:
  BlockOutputs(uint64_t offset, int height, std::vector<uint8_t>&& data)
      : height_(height), offset_(offset), data_(std::move(data)) {}
  BlockOutputs(BlockOutputs&&) = default;

  uint64_t BeginOffset() const { return offset_; }
  uint64_t EndOffset() const { return offset_ + data_.size(); }
  int Length() const { return std::ssize(data_); }
  int Height() const { return height_; }

  int FetchData(std::span<const OutputId> rids, std::span<const OutputDetail> outputs,
                uint8_t* buffer, size_t size) const;
  std::span<const uint8_t> Data() const { return data_; }

 protected:
  const int height_;
  const uint64_t offset_;
  std::vector<uint8_t> data_;
};

inline int BlockOutputs::FetchData(std::span<const OutputId> rids,
                                   std::span<const OutputDetail> outputs, uint8_t* buffer,
                                   size_t size) const {
  int count = 0;
  size_t cursor = 0;
  for (int i = 0; i < std::ssize(rids); ++i) {
    if (!outputs[i].header.IsNull()) continue;
    const auto decoded = IdCodec::Decode(rids[i]);
    const int address = decoded.offset - offset_;
    Assert(address >= 0);
    Assert(address + decoded.length <= std::ssize(data_));
    Assert(cursor + decoded.length <= size);
    std::memcpy(buffer + cursor, data_.data() + address, decoded.length);
    cursor += decoded.length;
    ++count;
  }
  return count;
}

}  // namespace hornet::data::utxo
