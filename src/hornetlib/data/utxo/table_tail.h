#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/codec.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

class TableTail {
 public:
  TableTail(uint64_t begin_offset = 0);

  OutputKV Append(protocol::TransactionConstView tx, int output, int height);
  void FetchData(std::span<const OutputId> ids, uint8_t* buffer, size_t size) const;
  std::optional<int> GetEarliestHeight() const;
  std::vector<uint8_t> CopyDataBefore(int height) const;
  void OnCommitBefore(int height);
  void EraseSince(int height);

 private:
  std::span<const uint8_t> Data(OutputId rid) const;
  OutputKV Append(const protocol::OutPoint& key, const OutputHeader& header, std::span<const uint8_t> script);

  int begin_height_;
  uint64_t base_offset_;  // The logical offset of the start of the tail's data.
  std::vector<uint32_t> bytes_per_height_;
  std::vector<uint8_t> data_;
};

inline TableTail::TableTail(uint64_t begin_offset /* = 0 */) : begin_height_(0), base_offset_(begin_offset) {
}

inline OutputKV TableTail::Append(protocol::TransactionConstView tx, int output, int height) {
  const protocol::OutPoint prevout{tx.GetHash(), static_cast<uint32_t>(output)};
  const OutputHeader header{height, 0, tx.Output(output).value};
  return Append(prevout, header, tx.PkScript(output));
}

inline OutputKV TableTail::Append(const protocol::OutPoint& key, const OutputHeader& header, std::span<const uint8_t> script) {
  const uint8_t* pheader = reinterpret_cast<const uint8_t*>(&header);
  const uint64_t address = base_offset_ + data_.size();
  data_.insert(data_.end(), pheader, pheader + sizeof(header));
  data_.insert(data_.end(), script.begin(), script.end());
  const int length = sizeof(header) + std::ssize(script);
  if (bytes_per_height_.empty()) begin_height_ = header.height;
  if (std::ssize(bytes_per_height_) < header.height - begin_height_ + 1)
    bytes_per_height_.resize(header.height - begin_height_ + 1, 0);
  bytes_per_height_[header.height - begin_height_] += length;
  return {key, {header.height, OutputKV::Add}, IdCodec::Encode(address, length)};
}

inline std::optional<int> TableTail::GetEarliestHeight() const {
  if (bytes_per_height_.empty()) return std::nullopt;
  return begin_height_;
}

inline std::span<const uint8_t> TableTail::Data(OutputId rid) const {
  const auto decode = IdCodec::Decode(rid);
  Assert(decode.offset >= base_offset_);
  const auto span = std::span{ data_.data() + decode.offset - base_offset_, static_cast<size_t>(decode.length) };
  Assert(span.end() <= data_.end());
  return span;
}

inline void TableTail::FetchData(std::span<const OutputId> ids, uint8_t* buffer, size_t size) const {
  int cursor = 0;
  for (OutputId id : ids) {
    const auto src = Data(id);
    if (cursor + src.size() > size) break;
    std::memcpy(buffer + cursor, src.data(), src.size_bytes());
    cursor += std::ssize(src);
  }
}

inline std::vector<uint8_t> TableTail::CopyDataBefore(int end_height) const {
  const int steps = std::min<int>(end_height - begin_height_, std::ssize(bytes_per_height_));
  uint32_t bytes_to_copy = 0;
  for (int i = 0; i < steps; ++i) bytes_to_copy += bytes_per_height_[i];

  std::vector<uint8_t> staging(bytes_to_copy);
  std::copy(data_.begin(), data_.begin() + bytes_to_copy, staging.begin());
  return staging;
}

inline void TableTail::OnCommitBefore(int end_height) {
  const int steps = std::min<int>(end_height - begin_height_, std::ssize(bytes_per_height_));
  uint32_t bytes_to_erase = 0;
  for (int i = 0; i < steps; ++i) bytes_to_erase += bytes_per_height_[i];
  Assert(bytes_to_erase <= data_.size());
  bytes_to_erase = std::min<uint32_t>(bytes_to_erase, data_.size());
  data_.erase(data_.begin(), data_.begin() + bytes_to_erase);
  bytes_per_height_.erase(bytes_per_height_.begin(), bytes_per_height_.begin() + steps);
  begin_height_ += steps;
  base_offset_ += bytes_to_erase;
}

inline void TableTail::EraseSince(int begin_height) {
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
