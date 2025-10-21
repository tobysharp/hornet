#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/unique_fd.h"

namespace hornet::data::utxo {

class Segments {
 public:
  Segments(const std::filesystem::path& folder);
  void Append(std::span<const uint8_t> data);
  uint64_t SizeBytes() const {
    return size_bytes_;
  }
  void FetchData(std::span<const OutputId> ids, uint8_t* buffer, size_t size) const;

 private:
  struct Item {
    std::filesystem::path path;
    UniqueFD fd_read;
    uint64_t offset;
    uint64_t length;
  };

  void OpenRead();
  int EnsureWriteFD(size_t bytes_to_write);
  int GetReadFD(uint64_t offset) const;
  static void Write(int fd, std::span<const uint8_t> bytes);

  std::filesystem::path folder_;
  std::vector<Item> items_;
  UniqueFD fd_write_;
  mutable UringIOEngine io_;
  std::atomic<uint64_t> size_bytes_ = 0;
  uint64_t max_segment_length_ = uint64_t(1) << 30;  // 1 GiB
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
  for (const auto& entry : entries) {
    const auto& path = entry.path();
    UniqueFD fd{::open(path.string().c_str(), O_RDONLY | O_CLOEXEC)};
    if (fd < 0) util::ThrowRuntimeError("Open failed: \"", path.string(), "\".");
    const uint64_t size = fs::file_size(path);
    items_.push_back({path, std::move(fd), offset, size});
    offset += size;
  }
  if (!entries.empty()) {
    fd_write_.Reset(::open(entries.back().path().string().c_str(),
                           O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR));
  }
  size_bytes_ = offset;
}

inline int Segments::EnsureWriteFD(size_t bytes_to_write) {
  if (items_.empty() || (items_.back().length + bytes_to_write > max_segment_length_)) {
    // Begin a new segment file.
    const auto path = folder_ / std::format("table_seg{:03d}.bin", std::ssize(items_));
    UniqueFD fd_write{
        ::open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, S_IRUSR | S_IWUSR)};
    UniqueFD fd_read{::open(
        path.c_str(), O_RDONLY | O_CLOEXEC)};  // TODO: Experiment with O_DIRECT to avoid page cache
    if (!fd_write || !fd_read)
      util::ThrowRuntimeError("File open failed.");  // TODO: Catch this somewhere

    // Add the new segment to the array.
    const uint64_t prev_end_offset =
        items_.empty() ? 0 : items_.back().offset + items_.back().length;
    items_.emplace_back(path, std::move(fd_read), prev_end_offset, 0);

    // Set the new append file.
    fd_write_ = std::move(fd_write);
  }
  return fd_write_;
}

inline int Segments::GetReadFD(uint64_t offset) const {
  Assert(!items_.empty() && offset < size_bytes_);
  const auto segment_it =
      std::upper_bound(items_.begin(), items_.end(), offset,
                       [](uint64_t offset, const Item& item) { return offset < item.offset; });
  --segment_it;
  Assert(segment_it->offset <= offset && offset < segment_it->offset + segment_it->length);
  return segment_it->fd_read;
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
  if (::fdatasync(fd) < 0) util::ThrowRuntimeError("fdatasync failed.");
}

inline void Segments::Append(std::span<const uint8_t> bytes) {
  if (bytes.empty()) return;
  Write(EnsureWriteFD(bytes.size()), bytes);
  items_.back().length += bytes.size();
  size_bytes_ = items_.back().offset + items_.back().length;
}

inline void Segments::FetchData(std::span<const OutputId> ids, uint8_t* buffer, size_t size) const {
  // Constructs the I/O requests, in the order passed.
  size_t cursor = 0;
  std::vector<IORequest> requests;
  requests.reserve(ids.size());
  int segment = 0;
  for (int i = 0; i < std::ssize(ids); ++i) {
    if (ids[i] == kNullOutputId) continue;
    // Retrieves the section index, byte offset, and byte length from a packed address.
    const auto [offset, length] = IdCodec::Decode(ids[i]);
    if (cursor + length > size) break;
    Assert(offset + length <= size_bytes_);
    while (offset >= items_[segment].offset + items_[segment].length) ++segment;
    requests.push_back(
        {items_[segment].fd_read, offset - items_[segment].offset, length, buffer + cursor, 0});
    cursor += length;
  }

  // Dispatch all the I/O requests to the I/O engine and wait for them to complete.
  Read(io_, requests);
}

}  // namespace hornet::data::utxo
