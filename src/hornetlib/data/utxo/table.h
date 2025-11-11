#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/atomic_vector.h"
#include "hornetlib/data/utxo/block_outputs.h"
#include "hornetlib/data/utxo/flusher.h"
#include "hornetlib/data/utxo/parallel.h"
#include "hornetlib/data/utxo/segments.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"

namespace hornet::data::utxo {

class Table {
 public:
  Table(const std::filesystem::path& folder);

  static void SortIds(std::span<OutputId> rids);

  int Fetch(std::span<const OutputId> ids, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) const;
  int AppendOutputs(const protocol::Block& block, int height, TiledVector<OutputKV>* entries);
  void EraseSince(int height);
  void CommitBefore(int height);
  void SetMutableWindow(int duration) noexcept;

 private:
  void EnqueueReadyCommits() noexcept;
  static int Unpack(
    std::span<const OutputId> rids, std::span<const uint8_t> staging, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts);

  Segments segments_;
  std::atomic<int> mutable_window_;
  AtomicVector<BlockOutputs> tail_;
  std::atomic<uint64_t> next_offset_;

  Flusher flusher_;  // Constructed last, destroyed first.
};

inline Table::Table(const std::filesystem::path& folder) : 
  segments_(folder), 
  mutable_window_(0),
  next_offset_(segments_.SizeBytes()),
  flusher_([this](int height) { CommitBefore(height); }) {
}

/* static */ inline void Table::SortIds(std::span<OutputId> rids) {
  ParallelSort(rids.begin(), rids.end());
}

/* static */ inline int Table::Unpack(
    std::span<const OutputId> rids, std::span<const uint8_t> staging, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) {
  int prev_script_size = std::ssize(*scripts);
  const size_t script_bytes = staging.size() - rids.size() * sizeof(OutputHeader);
  scripts->resize(prev_script_size + script_bytes);
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
  Assert(staging_cursor == staging.end());
  Assert(script_cursor == scripts->end());
  return written;
}

inline int Table::Fetch(std::span<const OutputId> rids, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) const {
  Assert(std::is_sorted(rids.begin(), rids.end(), [](OutputId lhs, OutputId rhs) { return IdCodec::Offset(lhs) < IdCodec::Offset(rhs); }));
  if (rids.empty()) return 0;

  // Determines the total byte count for sizing the staging buffer.
  size_t size = 0;
  for (const OutputId id : rids) size += IdCodec::Length(id);

  // Allocates the staging buffer.
  std::vector<uint8_t> staging(size);

  // Takes a snapshot of the tail now. Anything that's already been removed from the tail will be found in the main segments.
  const auto snapshot = tail_.Snapshot();
  Assert(IdCodec::Offset(rids.back()) < next_offset_);
  if (snapshot->empty()) {
    segments_.FetchData(rids, staging.data(), size);
    return Unpack(rids, staging, outputs, scripts);
  }
  
  // Initializes local variables for iterating over rids.
  auto next_block = snapshot->begin();
  const BlockOutputs* cur_block = nullptr;
  uint64_t next_boundary = snapshot->front()->BeginOffset();  // Start of first tail block.

  // Dispatches one FetchData call to the table segments or to a tail block.
  const auto dispatch_batch = [&](size_t begin_rid, size_t rid, size_t cursor, size_t block_bytes) {
    if (block_bytes > 0) {
      const auto subspan = rids.subspan(begin_rid, rid - begin_rid);
      uint8_t* dst = staging.data() + cursor;
      if (cur_block == nullptr)
        segments_.FetchData(subspan, dst, block_bytes);
      else
        cur_block->FetchData(subspan, dst, block_bytes);
    }
  };

  // Iterates over the rid's and the tail blocks, dispatching to FetchData at the appropriate boundaries.
  size_t begin_rid = 0;
  size_t cursor = 0;
  size_t block_bytes = 0;  
  for (size_t i = 0; i < rids.size(); ++i) {
    if (IdCodec::Offset(rids[i]) >= next_boundary)
    {
      // Dispatch to the newly completed block.
      dispatch_batch(begin_rid, i, cursor, block_bytes);
      cursor += block_bytes;
      block_bytes = 0;
      begin_rid = i;
    }
    while (IdCodec::Offset(rids[i]) >= next_boundary) {
      // Advance to the next block.
      Assert(next_block != snapshot->end());
      cur_block = next_block->get();
      ++next_block;
      next_boundary = cur_block->EndOffset();
    }
    block_bytes += IdCodec::Length(rids[i]);
  }
  dispatch_batch(begin_rid, rids.size(), cursor, block_bytes);
  Assert(cursor + block_bytes == size);

  // Unpacks the staged data into the output format.
  return Unpack(rids, staging, outputs, scripts);
}

inline int Table::AppendOutputs(const protocol::Block& block, int height, TiledVector<OutputKV>* entries) {
  // Calculates the number of bytes requires for this block's outputs.
  size_t bytes = 0;
  for (const auto tx : block.Transactions())
    for (int i = 0; i < tx.OutputCount(); ++i)
      bytes += sizeof(OutputHeader) + tx.PkScript(i).size();

  // Builds a local buffer holding the outputs.
  int count = 0;
  std::vector<uint8_t> data;
  data.reserve(bytes);
  const uint64_t offset = next_offset_.fetch_add(bytes);
  for (const auto tx : block.Transactions()) {
    for (int output = 0; output < tx.OutputCount(); ++output, ++count) {
      const protocol::OutPoint prevout{tx.GetHash(), static_cast<uint32_t>(output)};
      const OutputHeader header{height, 0, tx.Output(output).value};
      const auto pk_script = tx.PkScript(output);
      const uint8_t* pheader = reinterpret_cast<const uint8_t*>(&header);
      const uint64_t address = offset + data.size();
      data.insert(data.end(), pheader, pheader + sizeof(header));
      data.insert(data.end(), pk_script.begin(), pk_script.end());
      const int length = sizeof(header) + std::ssize(pk_script);
      const OutputKV kv{prevout, {height, OutputKV::Add}, IdCodec::Encode(address, length)};
      entries->PushBack(kv);
    }
  }

  // Publishes a new tail with the local buffer inserted in order.
  {
    auto edit = tail_.Edit();
    auto it = std::lower_bound(edit->begin(), edit->end(), offset, [](const std::shared_ptr<const BlockOutputs>& ptr, uint64_t base) {
      return ptr->BeginOffset() < base;
    });
    edit->insert(it, std::make_shared<BlockOutputs>(offset, height, std::move(data)));
  }

  // Enqueues a commit if the tail length is at least that of the mutable window size.
  EnqueueReadyCommits();

  // Returns the number of appended outputs.
  return count;
}

inline void Table::EraseSince(int height) {
  std::erase_if(*tail_.Edit(), [=](const std::shared_ptr<const BlockOutputs>& ptr) {
    return ptr->Height() >= height;
  });
}

inline void Table::CommitBefore(int height) {
  int blocks = 0;
  try {
    for (const auto& ptr : *tail_.Snapshot()) {
      if (ptr->Height() >= height) break;
      segments_.Append(ptr->Data());
      ++blocks;
    }
  } catch (const std::exception& e) { 
    LogError() << "Table::CommitBefore caught exception for height " << height 
               << ": \"" << e.what() << "\".";
  } catch (...) { 
    LogError() << "Table::CommitBefore caught exception for height " << height << ".";
  }
  tail_.EraseFront(blocks);
}

inline void Table::EnqueueReadyCommits() noexcept {
  const auto snapshot = tail_.Snapshot();
  if (snapshot->empty()) return;
  int min_height = snapshot->front()->Height();
  int max_height = min_height;
  for (const auto& ptr : *snapshot) {
    const int height = ptr->Height();
    if (height < min_height) min_height = height;
    if (height > max_height) max_height = height;
  }
  if (min_height + mutable_window_ <= max_height)
    flusher_.Enqueue(max_height + 1 - mutable_window_);
}

inline void Table::SetMutableWindow(int duration) noexcept {
  mutable_window_ = duration;
  EnqueueReadyCommits();
}

}  // namespace hornet::data::utxo
