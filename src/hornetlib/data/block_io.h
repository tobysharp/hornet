#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "hornetlib/protocol/block.h"
#include "hornetlib/util/io.h"
#include "hornetlib/util/iterator_range.h"
#include "hornetlib/util/throw.h"

namespace hornet::data {

class BlockWriter {
 public:
  BlockWriter(const std::filesystem::path& path)
      : path_{path}, stream_{path, std::ios::binary | std::ios::out | std::ios::trunc} {
    if (!stream_)
      util::ThrowRuntimeError("Failed to open block file \"", path_.string(), "\" for writing.");
    util::Write(stream_, 0);
    util::Write(stream_, 0ll);
  }

  ~BlockWriter() { 
    try {
      WriteIndex(); 
    } catch (...) {}
  }

  BlockWriter& operator<<(const protocol::Block& block) {
    const auto pos = stream_.tellp();
    offsets_.push_back(pos);
    block.Write(stream_);
    return *this;
  }

 private:
  void WriteIndex() {
    constexpr int32_t kVersion = 1;
    const int64_t index_offset = stream_.tellp();
    util::Write(stream_, static_cast<uint32_t>(offsets_.size()));
    for (auto offset : offsets_) util::Write(stream_, offset);
    stream_.seekp(0, std::ios::beg);
    util::Write(stream_, kVersion);
    util::Write(stream_, index_offset);
  }

  std::filesystem::path path_;
  std::ofstream stream_;
  std::vector<int64_t> offsets_;
};

class BlockReader {
 public:
  BlockReader(const std::filesystem::path& path)
      : path_{path}, stream_{path, std::ios::binary | std::ios::in} {
    if (!stream_)
      util::ThrowRuntimeError("Failed to open block file \"", path_.string(), "\" for reading.");
    ReadIndex();
  }

  int Size() const { return std::ssize(offsets_) - 1; }

  const BlockReader& operator>>(protocol::Block& block) const {
    block.Read(stream_);
    return *this;
  }

  std::shared_ptr<protocol::Block> operator[](int index) const {
    if (index < 0 || index >= Size())
      util::ThrowInvalidArgument("Block file index ", index, " out of range for file ",
                                 path_.string());

    const auto before = stream_.tellg();
    stream_.seekg(offsets_[index], std::ios::beg);
    const auto block = std::make_shared<protocol::Block>();
    operator>>(*block);
    const auto after = stream_.tellg();
    stream_.seekg(before, std::ios::beg);
    if (after != offsets_[index + 1])
      util::ThrowRuntimeError("Bad format in block file ", path_.string());
    return block;
  }

  class Iterator {
   public:
    Iterator(const BlockReader& reader, int index) : reader_(reader), index_(index) {}
    std::shared_ptr<protocol::Block> operator*() const { return reader_[index_]; }
    Iterator& operator++() { ++index_; return *this; }
    bool operator ==(const Iterator& rhs) const { return index_ == rhs.index_; }
    bool operator !=(const Iterator& rhs) const { return !operator==(rhs); }
   private:
    const BlockReader& reader_;
    int index_;
  };

  util::IteratorRange<Iterator> Blocks() const {
    return {Iterator{*this, 0}, Iterator{*this, Size()}};
  }

 private:
  void ReadIndex() {
    const int32_t version = util::Read<int32_t>(stream_);
    if (version != 1)
      util::ThrowRuntimeError("Unsupported block file version ", version, " in file ", path_.string());

    const int64_t index_offset = util::Read<int64_t>(stream_);
    stream_.seekg(index_offset, std::ios::beg);
    const auto count = util::Read<uint32_t>(stream_);
    offsets_.resize(count + 1);
    for (uint32_t i = 0; i < count; ++i) offsets_[i] = util::Read<std::int64_t>(stream_);
    offsets_.back() = index_offset;
    stream_.seekg(offsets_[0], std::ios::beg);
  }

  std::filesystem::path path_;
  mutable std::ifstream stream_;
  std::vector<int64_t> offsets_;
};

}  // namespace hornet::data
