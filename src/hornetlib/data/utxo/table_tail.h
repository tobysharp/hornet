#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class TableTail {
 public:
  OutputKV Append(protocol::TransactionConstView tx, int output, int height, uint64_t offset);
  std::optional<int> GetEarliestHeight() const;
  std::vector<uint8_t> CopyDataBefore(int height) const;
  void RemoveBefore(int height);
  void RemoveSince(int height);

  private:
  int begin_height_ = 0;
  std::vector<uint32_t> bytes_per_height_;
  std::vector<uint8_t> data_;
};

inline OutputKV TableTail::Append(protocol::TransactionConstView tx, int output, int height,
                             uint64_t offset) {
  Assert(std::ssize(bytes_per_height_) + begin_height_ <= height + 1);
  const OutputHeader header{height, 0, tx.Output(output).value};
  const uint8_t* pheader = reinterpret_cast<const uint8_t*>(&header);
  const auto script = tx.PkScript(output);
  const uint64_t address = offset + data_.size();
  data_.insert(data_.end(), pheader, pheader + sizeof(header));
  data_.insert(data_.end(), script.begin(), script.end());
  int length = sizeof(header) + std::ssize(script);
  if (bytes_per_height_.empty()) begin_height_ = height;
  bytes_per_height_.resize(height - begin_height_ + 1);
  bytes_per_height_[height - begin_height_] += length;
  return {{tx.GetHash(), output}, IdCodec::Encode(address, length)};
}

inline std::optional<int> TableTail::GetEarliestHeight() const {
  if (bytes_per_height_.empty()) return std::nullopt;
  return begin_height_;
}

inline std::vector<uint8_t> TableTail::CopyDataBefore(int end_height) const {
  const int steps = std::min<int>(end_height - begin_height_, std::ssize(bytes_per_height_));
  uint32_t bytes_to_copy = 0;
  for (int i = 0; i < steps; ++i) bytes_to_copy += bytes_per_height_[i];

  std::vector<uint8_t> staging(bytes_to_copy);
  std::copy(data_.begin(), data_.begin() + bytes_to_copy, staging.begin());
  return staging;
}

inline void TableTail::RemoveBefore(int end_height) {
  const int steps = std::min<int>(end_height - begin_height_, std::ssize(bytes_per_height_));
  uint32_t bytes_to_erase = 0;
  for (int i = 0; i < steps; ++i) bytes_to_erase += bytes_per_height_[i];
  Assert(bytes_to_erase <= data_.size());
  bytes_to_erase = std::min<uint32_t>(bytes_to_erase, data_.size());
  data_.erase(data_.begin(), data_.begin() + bytes_to_erase);
  bytes_per_height_.erase(bytes_per_height_.begin(), bytes_per_height_.begin() + steps);
  begin_height_ += steps;
}

inline void TableTail::RemoveSince(int begin_height) {
  const int end_height = begin_height_ + std::ssize(bytes_per_height_);
  const int steps = std::clamp<int>(end_height - begin_height, 0, std::ssize(bytes_per_height_));
  uint32_t bytes_to_erase = 0;
  for (int i = 0; i < steps; ++i) bytes_to_erase += bytes_per_height_[bytes_per_height_.size() - 1 - i];
  Assert(bytes_to_erase <= data_.size());
  bytes_to_erase = std::min<uint32_t>(bytes_to_erase, data_.size());
  data_.erase(data_.end() - bytes_to_erase, data_.end());
  bytes_per_height_.erase(bytes_per_height_.end() - steps, bytes_per_height_.end());
}

}  // namespace hornet::data::utxo
