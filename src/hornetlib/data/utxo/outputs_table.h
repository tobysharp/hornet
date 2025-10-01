#pragma once

#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

#include "hornetlib/util/subarray.h"

namespace hornet::data::utxo {

// A table of all outputs
class OutputsTable {
 public:
  OutputsTable();
  ~OutputsTable();

  // Set the age (in blocks) at which outputs are committed to the permanent table.
  void SetUndoWindow(int window) { 
    std::lock_guard lock(records_mutex_);
    undo_window_ = window;
  }

  // Append the fresh outputs minted in a block.
  template <typename Func>
  inline void AppendOutputs(const protocol::Block& block, Func&& on_output);

  // Removes all uncommitted outputs for block heights >= since.
  void RemoveRecentOutputs(int since);

 private:
  void RunCommitThread();

  struct OutputRecord {
    int height;
    uint32_t flags;
    int64_t amount;
    int script_size;
  };
  struct InMemoryRecord {
    OutputRecord details;
    int script_offset;
  };
  std::vector<InMemoryRecord> records_;
  std::vector<uint8_t> scripts_;      // The array of 
  std::mutex records_mutex_;          // Locks access to the in-memory append buffer.
  std::thread worker_;                // The background thread performs flush to disk and compaction.
  int height_ = -1;                   // The height of the last block appended to the table.
  std::atomic<bool> abort_ = false;   // Controls when the worker thread should terminate.
  std::condition_variable awake_;     // Controls when the worker thread should wake to do work.
  uint64_t size_bytes_ = 0;           // The total number of bytes of the whole table.
  int max_outputs_per_block_ = 0;     // The maximum number of outputs we've ever seen in a block.
  int undo_window_ = 0;               // The number of blocks to keep in memory before committing to disk.
  std::vector<uint8_t> staging_;      // The staging buffer used to organize data before writing to disk.
};

inline OutputsTable::OutputsTable() 
 : worker_(&OutputsTable::RunCommitThread, this) {
}

inline OutputsTable::~OutputsTable() {
  abort_ = true;
  if (worker_.joinable()) worker_.join();
}

inline void OutputsTable::RunCommitThread() {
  while (!abort_) {
    // When appropriate, move the in-memory records below the commit height into a 
    // staging buffer, and then write that buffer to the active segment's disk file.
    // We want to do this whenever we have "enough" records to write, but in practice
    // it's a cheap operation without significant overhead, so we don't need to delay.
    int max_commit_height;
    {
      std::unique_lock lock(records_mutex_);
      awake_.wait(lock, [&] {
        return !records_.empty() && records_[0].details.height <= height_ - undo_window_;
      });  // When we wake up, we have the lock on records_.

      max_commit_height = height_ - undo_window_;
      for (const InMemoryRecord& record : records_) {
        const OutputRecord& detail = record.details;
        if (detail.height > max_commit_height) break;
        const int offset = std::ssize(staging_);
        const std::span<const uint8_t> script = std::span{scripts_}.subspan(record.script_offset, detail.script_size);
        const int record_size = sizeof(OutputRecord) + std::ssize(script);
        // Resize the staging buffer to include this record. Any accumulation amortizes
        // as the buffer will never shrink once it has been extended.
        staging_.resize(offset + record_size);
        // Copy the record and script data into the 
        uint8_t* dst = &staging_[offset];
        std::memcpy(dst, &detail, sizeof(OutputRecord));
        std::memcpy(dst + sizeof(OutputRecord), script.data(), script.size());
      }
    }  // We can now release the lock on records_.

    // TODO: Get the fd for the active segment.

    // Flush the data to the segment, committing it permanently.
    int fd = -1;
    ::write(fd, staging_.data(), staging_.size());

    {
      // Remove the committed records from memory.
      std::unique_lock lock(records_mutex_);
      std::erase_if(records_, [&](const InMemoryRecord& record) {
        return record.details.height <= max_commit_height;
      });
    }

    // Clear the staging buffer, but leave its accumulated capacity for later reuse.
    staging_.clear();
  }
}

template <typename Func>
inline void OutputsTable::AppendOutputs(const protocol::Block& block, Func&& on_output) {
  std::lock_guard lock(records_mutex_);
  ++height_;
  int outputs = 0;
  for (const auto& tx : block.Transactions()) {
    for (int i = 0; i < tx.OutputCount(); ++i, ++outputs) {
      on_output(tx, i, size_bytes_);
      const auto pkspan = tx.PkScript(i);
      const auto start = scripts_.insert(scripts_.end(), pkspan.begin(), pkspan.end());
      records_.push_back({
        {
          .height = height_,
          .flags = 0,  // TODO
          .amount = tx.Output(i).value,
          .script_size = static_cast<int>(pkspan.size())
        }, 
        static_cast<int>(start - scripts_.begin())
      });
      size_bytes_ += sizeof(OutputRecord) + pkspan.size();
    }
  }
  max_outputs_per_block_ = std::max(max_outputs_per_block_, outputs);
  awake_.notify_one();
}

inline void OutputsTable::RemoveRecentOutputs(int since) {
  std::lock_guard lock(records_mutex_);
  std::vector<uint8_t> new_scripts;
  new_scripts.reserve(scripts_.size());
  for (InMemoryRecord& record : records_) {
    OutputRecord& detail = record.details;
    if (detail.height >= since) continue;
    const std::span<const uint8_t> script = std::span{scripts_}.subspan(record.script_offset, detail.script_size);
    const auto insert = new_scripts.insert(new_scripts.end(), script.begin(), script.end());
    detail.script_size = new_scripts.end() - insert;
    record.script_offset = insert - new_scripts.begin();
  }
  scripts_.swap(new_scripts);
  std::erase_if(records_, [&](const InMemoryRecord& record) { return record.details.height >= since; });
}

}  // namespace hornet::data::utxo
