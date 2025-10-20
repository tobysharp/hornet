#pragma once

#include "hornetlib/data/utxo/index.h"
#include "hornetlib/data/utxo/table.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace hornet::data::utxo {

class Database {
 public:
  // Constructs an unspent output database with the given duration (in blocks) as the recent
  // window, i.e. the period during which outputs may be removed before being permanently committed.
  Database(const std::filesystem::path& folder);

  // Queries the whole database for each prevout and writes their IDs into the equivalent slots of
  // ids. Returns the number of matches found.
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int height) const;

  // Fetches the output headers and script bytes for each ID.
  std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Fetch(
      std::span<const uint64_t> ids) const;

  // Appends all spendable outputs of the given block at the given height.
  void Append(const protocol::Block& block, int height);

  // Removes all outputs at heights greater than or equal to the given height. The given height
  // must be within the recent window compared to the highest block added. Otherwise the data
  // will already have been flushed to the permanently committed store.
  void RemoveSince(int height);

  void SetUndoDuration(int blocks);

 private:
  std::vector<uint8_t> Stage(std::span<const uint64_t> ids) const;
  static std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Unzip(
      std::span<const uint64_t> ids, std::span<uint8_t> staging);
  void CheckRethrowFatal() const {
    if (has_fatal_exception_) std::rethrow_exception(fatal_exception_);
  }
  static void AppendSpends(const protocol::Block& block, int height, TiledVector<OutputKV>* entries);

  Table table_;
  Index index_;
  int undo_duration_;
  std::atomic_bool has_fatal_exception_ = false;
  std::exception_ptr fatal_exception_;
  mutable std::shared_mutex mutex_;
};

inline Database::Database(const std::filesystem::path& folder, int recent_window)
    : committed_(folder), recent_window_(recent_window) {}

inline int Database::Query(std::span<const OutputKey> keys,
                           std::span<OutputId> rids, int height) const {
  CheckRethrowFatal();
  return index_.Query(keys, rids, height);
}

inline std::pair<std::vector<OutputDetail>, std::vector<uint8_t>> Database::Fetch(
    std::span<const uint64_t> ids) const {
  CheckRethrowFatal();
  return table_.Fetch(ids);
}

inline void Database::Append(const protocol::Block& block, int height) {
  CheckRethrowFatal();
  std::shared_lock lock(mutex_);
  TiledVector<OutputKV> entries;
  table_.AppendOutputs(block, height, &entries);
  AppendSpends(block, height, &entries);
  ParallelSort(entries.begin(), entries.end());
  index_.Append(std::move(entries), height);
}

inline void Database::RemoveSince(int height) {
  CheckRethrowFatal();
  std::unique_lock lock(mutex_);
  index_.RemoveSince(height);
  table_.RemoveSince(height);
}

/* static */ inline void Database::AppendSpends(const protocol::Block& block, int height, TiledVector<OutputKV>* entries) {
  for (const auto tx : block.Transactions())
    for (int i = 0; i < tx.OutputCount(); +++i) 
      entries->PushBack(OutputKV::Tombstone({tx.GetHash(), i}));
}

}  // namespace hornet::data::utxo
