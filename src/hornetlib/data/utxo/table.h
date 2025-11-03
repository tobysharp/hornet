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
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"

namespace hornet::data::utxo {

class Table {
 public:
  Table(const std::filesystem::path& folder);

  int Fetch(std::span<const OutputId> ids, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) const;
  int AppendOutputs(const protocol::Block& block, int height, TiledVector<OutputKV>* entries);
  void RemoveSince(int height);
  std::optional<int> GetEarliestTailHeight() const;
  void CommitBefore(int height);

 private:
  std::pair<std::span<const OutputId>, std::span<const OutputId>> Split(
      std::span<const OutputId> ids) const;
  static uint64_t CountSizeBytes(std::span<const OutputId> ids);
  static int Unpack(
    std::span<const OutputId> rids, std::span<const uint8_t> staging, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts);

  Segments segments_;
  TableTail tail_;
  mutable std::shared_mutex tail_mutex_;
};

Table::Table(const std::filesystem::path& folder) : segments_(folder), tail_(segments_.SizeBytes()) {
}

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

/* static */ inline int Table::Unpack(
    std::span<const OutputId> rids, std::span<const uint8_t> staging, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) {
  int prev_script_size = std::ssize(*scripts);
  scripts->resize(prev_script_size + staging.size() - rids.size() * sizeof(OutputHeader));
  auto staging_cursor = staging.begin();
  auto script_cursor = scripts->begin() + prev_script_size;
  int written = 0;
  for (int i = 0; i < std::ssize(rids); ++i) {
    if (rids[i] == kNullOutputId) continue;
    const auto length = IdCodec::Length(rids[i]);
    const int script_length = length - sizeof(OutputHeader);
    Assert(staging_cursor + length <= staging.end());
    Assert(script_cursor + script_length <= scripts->end());
    std::memcpy(&outputs[i].header, &*staging_cursor, sizeof(OutputHeader));
    std::memcpy(&*script_cursor, &*staging_cursor + sizeof(OutputHeader), script_length);
    outputs[i].script = {static_cast<int>(script_cursor - scripts->begin()), script_length};
    staging_cursor += length;
    script_cursor += script_length;
    ++written;
  }
  return written;
}

inline int Table::Fetch(std::span<const OutputId> ids, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) const {
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
  return Unpack(ids, staging, outputs, scripts);
}

inline int Table::AppendOutputs(const protocol::Block& block, int height, TiledVector<OutputKV>* entries) {
  int count = 0;
  {
    std::unique_lock lock(tail_mutex_);
    for (const auto tx : block.Transactions()) {
      for (int i = 0; i < tx.OutputCount(); ++i, ++count)
        entries->PushBack(tail_.Append(tx, i, height));
    }
  }
  return count;
}

inline void Table::RemoveSince(int height) {
  std::unique_lock lock(tail_mutex_);
  tail_.EraseSince(height);
}

inline std::optional<int> Table::GetEarliestTailHeight() const {
  std::shared_lock lock(tail_mutex_);
  return tail_.GetEarliestHeight();
}

inline void Table::CommitBefore(int height) {
  const auto staging = tail_.CopyDataBefore(height);
  segments_.Append(staging);
  {
    // This is where elements are removed from the tail. We need this not to overlap with the
    // part of Fetch that is trying to read these entries from the tail.
    std::unique_lock lock(tail_mutex_);
    tail_.OnCommitBefore(height);
  }
}

}  // namespace hornet::data::utxo
