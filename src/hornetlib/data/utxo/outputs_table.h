#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "hornetlib/data/utxo/io.h"
#include "hornetlib/data/utxo/search.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/data/utxo/unique_fd.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/subarray.h"

namespace hornet::data::utxo {

using IndexEntry = KeyValue<protocol::OutPoint, uint64_t>;

// A table of all outputs
class OutputsTable {
 public:
  OutputsTable(const std::string& folder);
  ~OutputsTable();

  // Set the age (in blocks) at which outputs are committed to the permanent table.
  void SetUndoWindow(int window) {
    undo_window_ = window;
  }

  void SetSegmentLength(uint64_t bytes) {
    max_segment_length_ = bytes;
  }

  // Append the fresh outputs minted in a block and advance the current block height.
  std::vector<IndexEntry> AppendOutputs(const protocol::Block& block, int height);

  // Removes all uncommitted outputs for block heights >= since.
  void RemoveRecentOutputs(int since);

  std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Fetch(
      std::span<const uint64_t> addresses) const;

  // Testing methods.
  IndexEntry AppendOutput(protocol::TransactionConstView tx, int output, int height,
                          bool wake = true);

 private:
  int CopyOutputsRaw(std::span<const uint64_t> addresses, uint8_t* buffer, int size) const;
  int GetOutputsSizeBytes(std::span<const uint64_t> addresses) const;

  struct Entry {
    OutputHeader record;
    util::SubArray<uint8_t> script;
  };
  struct Locator {
    int fd;
    uint64_t offset;
    int length;
  };
  struct Segment {
    UniqueFD fd_read;
    uint64_t offset;
    uint64_t length;
  };

  void RunCommitThread();
  Locator ParseAddress(uint64_t address) const;
  static uint64_t EncodeAddress(uint64_t offset, uint32_t length);
  void PrepareCurrentSegment(size_t bytes_to_write);
  void OpenSegments();

  // Collected data in the tail, prior to commit.
  std::vector<Entry> entries_;
  std::vector<uint8_t> scripts_; 
  int max_height_ = -1;  // The height of the tallest block appended to the table in this session.
  uint64_t size_bytes_ = 0;          // The total number of bytes of the whole table.
  int max_outputs_per_block_ = 0;    // The maximum number of outputs we've ever seen in a block.
  std::atomic<int> undo_window_ =
      0;  // The number of blocks to keep in memory before committing to disk.

  // Background thread that commits to the table.
  std::thread worker_;               // The background thread performs flush to disk and compaction.
  std::atomic<bool> abort_ = false;  // Controls when the worker thread should terminate.
  std::condition_variable awake_;    // Controls when the worker thread should wake to do work.
  std::mutex entries_mutex_;         // Locks access to the in-memory append buffer.

  // Filesystem and file descriptors.
  std::filesystem::path folder_;
  std::atomic<uint64_t> max_segment_length_ = (uint64_t)1 << 30;  // 1 GiB
  std::vector<Segment> segments_;
  UniqueFD fd_write_;

  // Asynchronous I/O reads.  
  mutable UringIOEngine io_;
  mutable std::vector<uint8_t>
      staging_;  // The staging buffer used to organize data before writing to disk.
};

inline OutputsTable::OutputsTable(const std::string& folder)
    : worker_(&OutputsTable::RunCommitThread, this), folder_(folder) {
  OpenSegments();
}

inline OutputsTable::~OutputsTable() {
  abort_ = true;
  awake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void OutputsTable::OpenSegments() {
  namespace fs = std::filesystem;
  std::vector<fs::directory_entry> entries;
  for (const auto& entry : fs::directory_iterator(folder_)) {
    if (entry.is_regular_file()) {
      const auto name = entry.path().filename().string();
      if (name.rfind("table_seg", 0) == 0) entries.push_back(entry);
    }
  }

  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return a.path().filename().string() < b.path().filename().string();
  });

  uint64_t offset = 0;
  for (const auto& entry : entries) {
    const auto& path = entry.path();
    UniqueFD fd{::open(path.string().c_str(), O_RDONLY | O_CLOEXEC)};
    if (fd < 0) util::ThrowRuntimeError("Open failed: \"", path.string(), "\".");
    uint64_t size = fs::file_size(path);
    segments_.push_back({std::move(fd), offset, size});
    offset += size;
  }
  if (!entries.empty()) {
    fd_write_.Reset(::open(entries.back().path().string().c_str(),
                           O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR));
  }
}

class AddressCodec {
 public:
  inline static std::pair<uint64_t, int> Decode(uint64_t address) {
    return {address >> kLengthBits, address & kLengthMask};
  }
  inline static uint64_t Encode(uint64_t offset, int length) {
    return (offset << kLengthBits) | (length & kLengthMask);
  }

 private:
  static constexpr int kLengthBits = 20;
  static constexpr int kLengthMask = (1 << kLengthBits) - 1;
  static constexpr int kOffsetBits = 64 - kLengthBits;
};

inline OutputsTable::Locator OutputsTable::ParseAddress(uint64_t address) const {
  const auto [offset, length] = AddressCodec::Decode(address);
  const auto segment_it =
      std::lower_bound(segments_.begin(), segments_.end(), offset,
                       [](const Segment& lhs, uint64_t rhs) { return lhs.offset < rhs; });
  return {segment_it->fd_read, offset - segment_it->offset, length};
}

inline uint64_t OutputsTable::EncodeAddress(uint64_t offset, uint32_t length) {
  return AddressCodec::Encode(offset, length);
}

inline void OutputsTable::PrepareCurrentSegment(size_t bytes_to_write) {
  if (segments_.empty() || (segments_.back().length + bytes_to_write > max_segment_length_)) {
    // Spill into new segment.
    const auto path = folder_ / std::format("table_seg{:03d}.bin", std::ssize(segments_));
    fd_write_.Reset(
        ::open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR));
    UniqueFD fd_read{::open(
        path.c_str(), O_RDONLY | O_CLOEXEC)};  // TODO: Experiment with O_DIRECT to avoid page cache
    if (!fd_write_ || !fd_read)
      util::ThrowRuntimeError("File open failed.");  // TODO: Catch this somewhere
    segments_.emplace_back(
        std::move(fd_read),
        segments_.empty() ? 0 : segments_.back().offset + segments_.back().length, 0);
  }
}

inline int OutputsTable::GetOutputsSizeBytes(std::span<const uint64_t> addresses) const {
  int bytes = 0;
  for (uint64_t address : addresses) bytes += ParseAddress(address).length;
  return bytes;
}

inline std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> OutputsTable::Fetch(
    std::span<const uint64_t> addresses) const {
  staging_.resize(GetOutputsSizeBytes(addresses));
  CopyOutputsRaw(addresses, staging_.data(), staging_.size());

  std::vector<OutputDetail> outputs(addresses.size());
  std::vector<uint8_t> scripts(staging_.size() - addresses.size() * sizeof(OutputHeader));
  auto staging_cursor = staging_.begin();
  auto script_cursor = scripts.begin();
  for (int i = 0; i < std::ssize(addresses); ++i) {
    const auto parsed = ParseAddress(addresses[i]);
    const int script_length = parsed.length - sizeof(OutputHeader);
    const OutputHeader header = *reinterpret_cast<const OutputHeader*>(*staging_cursor);
    outputs[i] = {header, {static_cast<int>(script_cursor - scripts.begin()), script_length}};
    std::memcpy(&*script_cursor, &*staging_cursor + sizeof(OutputHeader), script_length);
    staging_cursor += parsed.length;
    script_cursor += script_length;
  }
  return {outputs, scripts};
}

inline int OutputsTable::CopyOutputsRaw(std::span<const uint64_t> addresses, uint8_t* buffer,
                                        int size) const {
  // Constructs the I/O requests, in the order passed.
  int cursor = 0;
  std::vector<IORequest> requests;
  requests.reserve(addresses.size());
  for (int i = 0; i < std::ssize(addresses); ++i) {
    // Retrieves the section index, byte offset, and byte length from a packed address.
    const auto parsed = ParseAddress(addresses[i]);
    if (cursor + parsed.length > size) break;
    if (parsed.fd >= 0) {
      requests.push_back({.fd = parsed.fd,
                          .offset = parsed.offset,
                          .length = parsed.length,
                          .buffer = buffer + cursor,
                          .user = 0});
      cursor += parsed.length;
    } else {
      // Table row is in the in-memory tail.
      const Entry& entry = entries_[parsed.offset];
      const auto script = entry.script.Span(scripts_);
      std::memcpy(buffer + cursor, &entry.record, sizeof(OutputHeader));
      std::memcpy(buffer + cursor + sizeof(OutputHeader), script.data(), script.size());
      cursor += sizeof(OutputHeader) + script.size();
    }
  }

  // Dispatch all the I/O requests to the I/O engine.
  Read(io_, requests);
  return cursor;
}

inline void OutputsTable::RunCommitThread() {
  while (!abort_) {
    // When appropriate, move the in-memory records below the commit height into a
    // staging buffer, and then write that buffer to the active segment's disk file.
    // We want to do this whenever we have "enough" records to write, but in practice
    // it's a cheap operation without significant overhead, so we don't need to delay.
    int max_commit_height;
    {
      std::unique_lock lock(entries_mutex_);
      awake_.wait(lock, [&] {
        return abort_ ||
               (!entries_.empty() && entries_[0].record.height <= max_height_ - undo_window_);
      });  // When we wake up, we have the lock on records_.
      if (abort_) break;

      max_commit_height = max_height_ - undo_window_;
      for (const Entry& entry : entries_) {
        const OutputHeader& record = entry.record;
        if (record.height > max_commit_height) break;
        const int offset = std::ssize(staging_);
        const auto script = entry.script.Span(scripts_);
        // Resize the staging buffer to include this record. Any accumulation amortizes
        // as the buffer will never shrink once it has been extended.
        staging_.resize(offset + sizeof(OutputHeader) + script.size());
        // Copy the record and script data into the staging buffer.
        uint8_t* dst = &staging_[offset];
        std::memcpy(dst, &record, sizeof(OutputHeader));
        std::memcpy(dst + sizeof(OutputHeader), script.data(), script.size());
      }
    }  // We can now release the lock on records_.

    // Flush the data to the segment, committing it permanently.
    PrepareCurrentSegment(staging_.size());
    size_t written = 0;
    while (written < staging_.size()) {
      ssize_t n = ::write(fd_write_, staging_.data() + written, staging_.size() - written);
      if (n <= 0) util::ThrowRuntimeError("Write failed.");
      written += n;
    }
    ::fdatasync(fd_write_);
    segments_.back().length += staging_.size();

    {
      // Remove the committed records from memory.
      std::unique_lock lock(entries_mutex_);
      std::erase_if(entries_,
                    [&](const Entry& entry) { return entry.record.height <= max_commit_height; });
    }

    // Clear the staging buffer, but leave its accumulated capacity for later reuse.
    staging_.clear();
  }
}

inline IndexEntry OutputsTable::AppendOutput(protocol::TransactionConstView tx, int output,
                                             int height, bool wake /* = true */) {
  const auto pkspan = tx.PkScript(output);
  const auto start = scripts_.insert(scripts_.end(), pkspan.begin(), pkspan.end());
  entries_.push_back(
      {{
           .height = height,
           .flags = 0,  // TODO
           .amount = tx.Output(output).value,
       },
       {static_cast<int>(start - scripts_.begin()), static_cast<int>(pkspan.size())}});
  const uint32_t length = static_cast<uint32_t>(sizeof(OutputHeader) + pkspan.size());
  const uint64_t address = EncodeAddress(size_bytes_, length);
  size_bytes_ += length;
  max_height_ = std::max(max_height_, height);
  if (wake) awake_.notify_all();
  return {{tx.GetHash(), static_cast<uint32_t>(output)}, address};
}

inline std::vector<IndexEntry> OutputsTable::AppendOutputs(const protocol::Block& block,
                                                           int height) {
  std::vector<IndexEntry> rv;

  std::lock_guard lock(entries_mutex_);
  int outputs = 0;
  for (const auto& tx : block.Transactions()) {
    for (int i = 0; i < tx.OutputCount(); ++i, ++outputs)
      rv.push_back(AppendOutput(tx, i, height, false));
  }
  max_outputs_per_block_ = std::max(max_outputs_per_block_, outputs);
  awake_.notify_all();
  return rv;
}

inline void OutputsTable::RemoveRecentOutputs(int since) {
  std::lock_guard lock(entries_mutex_);
  std::vector<uint8_t> new_scripts;
  new_scripts.reserve(scripts_.size());
  for (Entry& entry : entries_) {
    OutputHeader& record = entry.record;
    if (record.height >= since) continue;
    const std::span<const uint8_t> pkspan = entry.script.Span(scripts_);
    const auto start = new_scripts.insert(new_scripts.end(), pkspan.begin(), pkspan.end());
    entry.script = {static_cast<int>(start - scripts_.begin()), static_cast<int>(pkspan.size())};
  }
  scripts_.swap(new_scripts);
  std::erase_if(entries_, [&](const Entry& entry) { return entry.record.height >= since; });
  max_height_ = since - 1;
}

}  // namespace hornet::data::utxo
