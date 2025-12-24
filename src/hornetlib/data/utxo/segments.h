#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include <fcntl.h>

#include "hornetlib/data/utxo/atomic_vector.h"
#include "hornetlib/data/utxo/codec.h"
#include "hornetlib/data/utxo/io.h"
#include "hornetlib/data/utxo/types.h"
#include "hornetlib/data/utxo/unique_fd.h"

namespace hornet::data::utxo {

class Segments {
 public:
  Segments(const std::filesystem::path& folder);
  void Append(std::span<const uint8_t> data);
  uint64_t SizeBytes() const { 
    const auto items = items_.Snapshot();
    return items->empty() ? 0 : items->back()->offset + items->back()->length;
  }
  int FetchData(std::span<const OutputId> ids, std::span<const OutputDetail> outputs,
                uint8_t* buffer, size_t size) const;

 private:
  struct Item {
    std::filesystem::path path;
    std::shared_ptr<UniqueFD> fd_read;
    uint64_t offset;
    uint64_t length;
  };

  void OpenRead();
  int EnsureWriteFD(AtomicVector<Item>::Writer& edit, size_t bytes_to_write);
  int GetReadFD(uint64_t offset) const;
  static void Write(int fd, std::span<const uint8_t> bytes);

  std::filesystem::path folder_;
  AtomicVector<Item> items_;
  UniqueFD fd_write_;
  static constexpr uint64_t max_segment_length_ = uint64_t(1) << 30;  // 1 GiB
};

inline Segments::Segments(const std::filesystem::path& folder) : folder_(folder) {
  OpenRead();
}

inline void Segments::OpenRead() {
  namespace fs = std::filesystem;
  std::vector<fs::directory_entry> entries;
  for (const auto& entry : fs::directory_iterator(folder_)) {
    if (entry.is_regular_file()) {
      const auto name = entry.path().filename().string();
      if (name.rfind("table_seg", 0) == 0 && name.ends_with(".bin")) entries.push_back(entry);
    }
  }

  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return a.path().filename().string() < b.path().filename().string();
  });

  uint64_t offset = 0;
  auto edit = items_.Edit();
  edit->reserve(entries.size());
  for (const auto& entry : entries) {
    const auto& path = entry.path();
    UniqueFD fd{::open(path.string().c_str(), O_RDONLY | O_CLOEXEC)};
    if (fd < 0) util::ThrowRuntimeError("Open failed: \"", path.string(), "\".");
    const uint64_t size = fs::file_size(path);
    edit->emplace_back(std::make_shared<Item>(Item{path, std::make_shared<UniqueFD>(std::move(fd)), offset, size}));
    offset += size;
  }
  if (!entries.empty()) {
    fd_write_.Reset(::open(entries.back().path().string().c_str(),
                           O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR));
  }
}

inline int Segments::EnsureWriteFD(AtomicVector<Item>::Writer& edit, size_t bytes_to_write) {
  if (edit->empty() || (edit->back()->length + bytes_to_write > max_segment_length_)) {
    // Begin a new segment file.
    const auto path = folder_ / std::format("table_seg{:03d}.bin", std::ssize(*edit));
    UniqueFD fd_write{
        ::open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    UniqueFD fd_read{::open(
        path.c_str(), O_RDONLY | O_CLOEXEC)};  // TODO: Experiment with O_DIRECT to avoid page cache
    if (!fd_write || !fd_read)
      util::ThrowRuntimeError("File open failed.");  // TODO: Catch this somewhere

    // Add the new segment to the array.
    const uint64_t prev_end_offset =
        edit->empty() ? 0 : edit->back()->offset + edit->back()->length;
    edit->emplace_back(std::make_shared<Item>(Item{path, std::make_shared<UniqueFD>(std::move(fd_read)), prev_end_offset, 0}));

    // Set the new append file.
    fd_write_ = std::move(fd_write);
  }
  return fd_write_;
}

inline int Segments::GetReadFD(uint64_t offset) const {
  const auto items = items_.Snapshot();
  Assert(!items->empty() && offset < SizeBytes());
  auto segment_it =
      std::upper_bound(items->begin(), items->end(), offset,
                       [](uint64_t offset, const auto& item) { return offset < item->offset; });
  --segment_it;
  Assert((*segment_it)->offset <= offset && offset < (*segment_it)->offset + (*segment_it)->length);
  return *(*segment_it)->fd_read;
}

/* static */ inline void Segments::Write(int fd, std::span<const uint8_t> bytes) {
  ssize_t written = 0;
  while (written < std::ssize(bytes)) {
    ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      util::ThrowRuntimeError("Write failed.");
    }
    written += n;
  }
  // if (::fdatasync(fd) < 0) util::ThrowRuntimeError("fdatasync failed.");
 }

inline void Segments::Append(std::span<const uint8_t> bytes) {
  if (bytes.empty()) return;
  auto edit = items_.Edit();
  int fd = EnsureWriteFD(edit, bytes.size());
  try {
    Write(fd, bytes);
    auto replace_item = std::make_shared<Item>(*edit->back());
    replace_item->length += bytes.size();
    edit->back() = replace_item;
  } catch (...) {
    // Roll back partial write.
    ::ftruncate(fd, edit->back()->length);
    edit.Cancel();
    throw;
  }
}

inline int Segments::FetchData(std::span<const OutputId> ids, std::span<const OutputDetail> outputs,
                               uint8_t* buffer, size_t size) const {
  // Constructs the I/O requests, in the order passed.
  size_t cursor = 0;
  std::vector<IORequest> requests;
  requests.reserve(ids.size());
  int segment = 0;
  const auto snapshot = items_.Snapshot();
  const auto& items = *snapshot;
  const auto size_bytes = items.empty() ? 0 : items.back()->offset + items.back()->length;
  for (int i = 0; i < std::ssize(ids); ++i) {
    if (!IsOutputIdValid(ids[i]) || !outputs[i].header.IsNull()) continue;
    // Retrieves the section index, byte offset, and byte length from a packed address.
    const auto [offset, length] = IdCodec::Decode(ids[i]);
    if (cursor + length > size) break;
    Assert(offset + length <= size_bytes);
    while (offset >= items[segment]->offset + items[segment]->length) ++segment;
    requests.push_back(
        {*items[segment]->fd_read, offset - items[segment]->offset, length, buffer + cursor, 0});
    cursor += length;
  }

  // Dispatch all the I/O requests to the I/O engine and wait for them to complete.
  static thread_local UringIOEngine io;
  Read(io, requests);
  return std::ssize(requests);
}

}  // namespace hornet::data::utxo
