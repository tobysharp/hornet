#pragma once

#include "hornetlib/data/utxo/index.h"
#include "hornetlib/data/utxo/table.h"
#include "hornetlib/data/utxo/tiled_vector.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

#include <atomic>
#include <cstdint>
#include <exception>
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

  static void SortKeys(std::span<OutputKey> keys);
  static void SortIds(std::span<OutputId> rids);

  // Queries the whole database for each prevout and writes their IDs into the equivalent slots of
  // ids. Returns the number of matches found.
  int Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int before) const {
    return Query(keys, rids, 0, before).funded;
  }

  QueryResult Query(std::span<const OutputKey> keys, std::span<OutputId> rids, int since, int before) const;

  // Fetches the output headers and script bytes for each ID.
  int Fetch(std::span<const uint64_t> ids, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) const;

  // Appends all spendable outputs of the given block at the given height.
  void Append(const protocol::Block& block, int height);

  // Removes all outputs at heights greater than or equal to the given height. The given height
  // must be within the recent window compared to the highest block added. Otherwise the data
  // will already have been flushed to the permanently committed store.
  void EraseSince(int height);

  void SetMutableWindow(int heights);

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
  std::atomic_bool has_fatal_exception_ = false;
  std::exception_ptr fatal_exception_;
  mutable std::shared_mutex mutex_;
};

inline Database::Database(const std::filesystem::path& folder)
    : table_(folder) {}

inline QueryResult Database::Query(std::span<const OutputKey> keys,
                           std::span<OutputId> rids, int since, int before) const {
  CheckRethrowFatal();
  return index_.Query(keys, rids, since, before);
}

inline int Database::Fetch(std::span<const OutputId> rids, std::span<OutputDetail> outputs, std::vector<uint8_t>* scripts) const {
  CheckRethrowFatal();
  return table_.Fetch(rids, outputs, scripts);
}

inline void Database::Append(const protocol::Block& block, int height) {
  CheckRethrowFatal();
  std::shared_lock lock(mutex_);
  auto entries = index_.MakeAppendBuffer();
  table_.AppendOutputs(block, height, &entries);
  AppendSpends(block, height, &entries);
  ParallelSort(entries.begin(), entries.end());
  index_.Append(std::move(entries), height);
}

/* static */ void Database::SortKeys(std::span<OutputKey> keys) {
  Index::SortKeys(keys);
}

/* static */ void Database::SortIds(std::span<OutputId> rids) {
  Table::SortIds(rids);
}

inline void Database::EraseSince(int height) {
  CheckRethrowFatal();
  std::unique_lock lock(mutex_);
  index_.EraseSince(height);
  table_.EraseSince(height);
}

/* static */ inline void Database::AppendSpends(const protocol::Block& block, int height, TiledVector<OutputKV>* entries) {
  for (const auto tx : block.Transactions()) {
    for (int i = 0; i < tx.InputCount(); ++i) {
      const auto& prevout = tx.Input(i).previous_output;
      if (!prevout.IsNull())
        entries->PushBack(OutputKV::Tombstone(prevout, height));
    }
  }
}

inline void Database::SetMutableWindow(int heights) {
  if (heights > Index::GetMutableWindow()) 
    util::ThrowInvalidArgument("SetMutableWindow: ", heights, " exceeds Index geometry.");
  table_.SetMutableWindow(heights);
}

}  // namespace hornet::data::utxo
