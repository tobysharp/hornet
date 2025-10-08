#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <shared_mutex>
#include <span>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/segments.h"
#include "hornetlib/data/utxo/table_tail.h"
#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

class Table {
 public:
  Table(const std::filesystem::path& folder);

  std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Fetch(
      std::span<const OutputId> ids) const;
  std::vector<OutputKV> AppendTail(const protocol::Block& block, int height);
  void RemoveSince(int height);
  std::optional<int> GetEarliestTailHeight() const;
  void CommitBefore(int height);

 private:
  std::pair<std::span<const OutputId>, std::span<const OutputId>> Split(
      std::span<const OutputId> ids) const;
  static uint64_t CountSizeBytes(std::span<const OutputId> ids);
  static std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Unpack(
    std::span<const OutputId> ids, std::span<const uint8_t> staging);

  Segments segments_;
  TableTail tail_;
  mutable std::shared_mutex tail_mutex_;
};

/* static */ inline uint64_t Table::CountSizeBytes(std::span<const OutputId> ids) {
  uint64_t bytes = 0;
  for (OutputId id : ids) bytes += IdCodec::Length(id);
  return bytes;
}

inline std::pair<std::span<const OutputId>, std::span<const OutputId>> Table::Split(
    std::span<const OutputId> ids) const {
  const uint64_t tail_offset = segments_.SizeBytes();
  const auto tail_it =
      std::lower_bound(ids.begin(), ids.end(), tail_offset,
                       [](OutputId id, uint64_t offset) { return IdCodec::Offset(id) < offset; });
  const auto tail_index = std::distance(ids.begin(), tail_it);
  const auto segment_ids = ids.subspan(0, tail_index);
  const auto tail_ids = ids.subspan(tail_index);
  return {segment_ids, tail_ids};
}

/* static */ inline std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Table::Unpack(
    std::span<const OutputId> ids, std::span<const uint8_t> staging) {
  std::vector<OutputDetail> outputs(ids.size());
  std::vector<uint8_t> scripts(staging.size() - ids.size() * sizeof(OutputHeader));
  auto staging_cursor = staging.begin();
  auto script_cursor = scripts.begin();
  for (int i = 0; i < std::ssize(ids); ++i) {
    const auto length = IdCodec::Length(ids[i]);
    const int script_length = length - sizeof(OutputHeader);
    Assert(staging_cursor + length <= staging.end());
    Assert(script_cursor + script_length <= scripts.end());
    std::memcpy(&outputs[i].header, &*staging_cursor, sizeof(OutputHeader));
    std::memcpy(&*script_cursor, &*staging_cursor + sizeof(OutputHeader), script_length);
    outputs[i].script = {static_cast<int>(script_cursor - scripts.begin()), script_length};
    staging_cursor += length;
    script_cursor += script_length;
  }
  return {outputs, scripts};
}

inline std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Table::Fetch(
    std::span<const OutputId> ids) const {
  std::vector<uint8_t> staging(CountSizeBytes(ids));

  std::span<const OutputId> segment_ids, tail_ids;
  uint64_t segment_bytes = 0;
  {
    // Take a read lock on the tail to prevent a commit removing items from the tail after
    // we got a value for segments_.SizeBytes() and before we fetched the items out of the tail.
    std::shared_lock lock(tail_mutex_);
    std::tie(segment_ids, tail_ids) = Split(ids);
    segment_bytes = CountSizeBytes(segment_ids);
    tail_.FetchData(tail_ids, staging.data() + segment_bytes, staging.size() - segment_bytes);
  }
  // Since the segments are append-only, we don't need to lock here.
  segments_.FetchData(segment_ids, staging.data(), segment_bytes);
  return Unpack(ids, staging);
}

inline std::vector<OutputKV> Table::AppendTail(const protocol::Block& block, int height) {
  std::vector<OutputKV> kvs;
  {
    std::unique_lock lock(tail_mutex_);
    const uint64_t offset = segments_.SizeBytes();
    for (const auto tx : block.Transactions()) {
      for (int i = 0; i < tx.OutputCount(); ++i) kvs.push_back(tail_.Append(tx, i, height, offset));
    }
  }
  return kvs;
}

inline void Table::RemoveSince(int height) {
  std::unique_lock lock(tail_mutex_);
  tail_.RemoveSince(height);
}

inline std::optional<int> Table::GetEarliestTailHeight() const {
  std::shared_lock lock(tail_mutex_);
  return tail_.GetEarliestHeight();
}

inline void Table::CommitBefore(int height) {
  const auto staging = tail_.CopyDataBefore(height);
  segments_.Append(staging, height);
  {
    // This is where elements are removed from the tail. We need this not to overlap with the
    // part of Fetch that is trying to read these entries from the tail.
    std::unique_lock lock(tail_mutex_);
    tail_.RemoveBefore(height);
  }
}

}  // namespace hornet::data::utxo
