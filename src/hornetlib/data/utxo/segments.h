#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <tuple>
#include <vector>

#include "hornetlib/data/utxo/unique_fd.h"

namespace hornet::data::utxo {

class Segments {
 public:
  struct Item {
    std::filesystem::path path;
    UniqueFD fd_read;
    uint64_t offset;
    uint64_t length;
    std::pair<int, int> heights;
  };
  Segments(const std::filesystem::path& folder);
  void Append(std::span<const uint8_t> data, int end_height);
  uint64_t Size() const {
    return size_bytes_;
  }

 private:
  int GetWriteFD(size_t bytes_to_write);
 
  std::filesystem::path folder_;
  std::vector<Item> items_;
  UniqueFD fd_write_;
  std::atomic<uint64_t> size_bytes_;
  uint64_t max_segment_length_ = uint64_t(1) << 30;  // 1 GiB
};

inline int Segments::GetWriteFD(size_t bytes_to_write) {
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
    const uint64_t prev_end_offset = items_.empty() ? 0 : items_.back().offset + items_.back().length;
    const int prev_end_height = items_.empty() ? 0 : items_.back().heights.second;
    items_.emplace_back(
        path,
        std::move(fd_read),
        prev_end_offset
        0,
        std::make_pair(prev_end_height, prev_end_height));
    
    // Set the new append file.
    fd_write_ = std::move(fd_write);
  }
  return fd_write_;
}

inline void Segments::Append(std::span<const uint8_t> bytes, int end_height) {
  if (bytes.empty()) return true;
  int fd_write = GetWriteFD(bytes.size());
  ssize_t written = 0;
  while (written < std::ssize(bytes)) {
    ssize_t n = ::write(fd_write, bytes.data() + written, bytes.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      util::ThrowRuntimeError("Write failed.");
    }
    written += n;
  }
  if (::fdatasync(fd_write) < 0)
    util::ThrowRuntimeError("fdatasync failed.");
  items_.back().heights.second = end_height;
  items_.back().length += bytes.size();
  size_bytes_ = items_.back().offset + items_.back().length;
}

}  // namespace hornet::data::utxo
