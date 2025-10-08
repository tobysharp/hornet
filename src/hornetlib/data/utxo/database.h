#pragma once

#include "hornetlib/data/utxo/codec.h"
#include "hornetlib/data/utxo/committed.h"
#include "hornetlib/data/utxo/recent.h"
#include "hornetlib/data/utxo/types.h"

#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace hornet::data::utxo {

class Database {
 public:
  // Constructs an unspent output database with the given duration (in blocks) as the recent
  // window, i.e. the period during which outputs may be removed before being permanently committed.
  Database(const std::filesystem::path& folder, int recent_window);

  // Queries the whole database for each prevout and writes their IDs into the equivalent slots of
  // ids. Returns the number of matches found.
  int Query(std::span<const protocol::OutPoint> prevouts, std::span<uint64_t> ids) const;

  // Queries the recently appended items for each prevout and writes their IDs into the equivalent
  // slot of ids. Returns the number of matches found.
  int QueryTail(std::span<const protocol::OutPoint> prevouts, std::span<uint64_t> ids) const;

  // Queries the committed items for each prevout and writes their IDs into the equivalent
  // slot of ids. Returns the number of matches found.
  int QueryMain(std::span<const protocol::OutPoint> prevouts, std::span<uint64_t> ids) const;

  // Fetches the output headers and script bytes for each ID.
  std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Fetch(
      std::span<const uint64_t> ids) const;

  // Appends all spendable outputs of the given block at the given height.
  void AppendTail(const protocol::Block& block, int height);

  // Removes all outputs at heights greater than or equal to the given height. The given height
  // must be within the recent window compared to the highest block added. Otherwise the data
  // will already have been flushed to the permanently committed store.
  void RemoveSince(int height);

  // Returns true if there is data in the recent store waiting to be committed to the permanent
  // store.
  int GetCommittableBlockCount() const;

  // Commits data from the recent store that is older than the recent window.
  void CommitTail();

  void SetUndoDuration(int blocks);

 private:
  std::vector<uint8_t> Stage(std::span<const uint64_t> ids) const;
  static std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Unzip(
      std::span<const uint64_t> ids, std::span<uint8_t> staging);
  void CheckRethrowFatal() const {
    if (has_fatal_exception_) std::rethrow_exception(fatal_exception_);
  }

  Table table_;
  Index index_;
  int undo_duration_;
  std::atomic_bool has_fatal_exception_ = false;
  std::exception_ptr fatal_exception_;
  mutable std::shared_mutex mutex_;
};

inline Database::Database(const std::filesystem::path& folder, int recent_window)
    : committed_(folder), recent_window_(recent_window) {}

inline int Database::Query(std::span<const protocol::OutPoint> prevouts,
                           std::span<uint64_t> ids) const {
  return QueryCommitted(prevouts, ids) + QueryRecent(prevouts, ids);
}

inline int Database::QueryTail(std::span<const protocol::OutPoint> prevouts,
                               std::span<uint64_t> ids) const {
  CheckRethrowFatal();
  return index_.QueryTail(prevouts, ids);
}

inline int Database::QueryMain(std::span<const protocol::OutPoint> prevouts,
                               std::span<uint64_t> ids) const {
  CheckRethrowFatal();
  return index_.QueryMain(prevouts, ids);
}

inline std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Database::Fetch(
    std::span<const uint64_t> ids) const {
  CheckRethrowFatal();
  return table_.Fetch(ids);
}

void Database::AppendTail(const protocol::Block& block, int height) {
  CheckRethrowFatal();
  std::shared_lock lock(mutex_);
  const auto kv_pairs = table_.AppendTail(block, height);
  index_.AppendTail(kv_pairs, height);
}

void Database::RemoveSince(int height) {
  CheckRethrowFatal();
  std::unique_lock lock(mutex_);
  index_.RemoveSince(height);
  table_.RemoveSince(height);
}

int Database::GetCommittableBlockCount() const {
  CheckRethrowFatal();
  std::shared_lock lock(mutex_);
  int end_height = GetLatestHeight() - undo_duration_ + 1;
  int earliest_tail = std::min(table_.GetEarliestTailHeight(), index_.GetEarliestTailHeight());
  return end_height - earliest_tail;
}

void Database::CommitTail() {
  CheckRethrowFatal();
  std::shared_lock lock(mutex_);
  try {
    int end_height = GetLatestHeight() - undo_duration_ + 1;
    const auto marker = MutationMarker(std::format("CommitBefore: {}", end_height));
    table_.CommitBefore(end_height);
    index_.CommitBefore(end_height);
  } catch (...) {
    if (!is_fatal_exception_.exchange(true)) {
      fatal_exception_ = std::current_exception();
      try {
        CheckRethrowFatal();
      } catch (const std::exception& e) {
        LogError("Database::CommitTail: ", e.what());
      } catch (...) {
        LogError("Database::CommitTail: Caught exception.");
      }
    }
    throw;
  }
}

// inline std::vector<uint8_t> Database::Stage(std::span<const uint64_t> ids) const {
//   // Pre-allocates data for staging.
//   int staging_bytes = 0;
//   for (uint64_t id : ids) staging_bytes += IdCodec::Decode(id).second;
//   std::vector<uint8_t> staging(staging_bytes);

//   // We take a brief lock to prevent simultaneous commit mutation. Then we decode each ID and
//   either
//   // convert it into an I/O request or copy the data straight out of the tail.
//   std::vector<IORequest> requests;
//   requests.reserve(ids.size());
//   {
//     std::lock_guard lock(&commit_mutex_);
//     int cursor = 0;
//     const uint64_t committed_size = committed_.Size();
//     for (int i = 0; i < std::ssize(ids); ++i) {
//       const auto [offset, length] = IdCodec::Decode(ids[i]);
//       if (offset < committed_size) {
//         requests.push_back(committed_.MakeIORequest(ids[i], staging.data() + cursor,
//                                                     staging_bytes - cursor));
//         cursor += length;
//       } else {
//         cursor +=
//             recent_.Fetch(offset, length, staging.data() + cursor, staging_bytes - cursor);
//       }
//     }
//   }
//   Assert(cursor == staging_bytes);

//   // Dispatch all the I/O requests to the I/O engine.
//   committed_.Fetch(requests);
//   return staging;
// }

// inline /* static */ std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Database::Unzip(
//     std::span<const uint64_t> ids, std::span<uint8_t> staging) {
//   std::vector<OutputDetail> outputs(ids.size());
//   std::vector<uint8_t> scripts(staging.size() - ids.size() * sizeof(OutputHeader));
//   auto staging_cursor = staging.begin();
//   auto script_cursor = scripts.begin();
//   for (int i = 0; i < std::ssize(ids); ++i) {
//     const int length = IdCodec::Decode(ids[i]).second;
//     const int script_length = length - sizeof(OutputHeader);
//     const OutputHeader* header = reinterpret_cast<const OutputHeader*>(&*staging_cursor);
//     outputs[i] = {*header, {static_cast<int>(script_cursor - scripts.begin()), script_length}};
//     std::memcpy(&*script_cursor, &*staging_cursor + sizeof(OutputHeader), script_length);
//     staging_cursor += length;
//     script_cursor += script_length;
//   }
//   Assert(staging_cursor == staging.end());
//   Assert(script_cursor == scripts.end());

//   return {outputs, scripts};
// }

}  // namespace hornet::data::utxo
