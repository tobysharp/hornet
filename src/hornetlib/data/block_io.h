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
    } catch (...) {
    }
  }

  BlockWriter& operator<<(const protocol::Block& block) {
    const auto pos = stream_.tellp();
    offsets_.push_back(pos);
    block.Write(stream_);
    return *this;
  }

  int64_t SizeBytes() const { return stream_.tellp(); }

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
  mutable std::ofstream stream_;
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

  bool IsEof() const { return stream_.tellg() >= offsets_.back(); }

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
    Iterator& operator++() {
      ++index_;
      return *this;
    }
    bool operator==(const Iterator& rhs) const { return index_ == rhs.index_; }
    bool operator!=(const Iterator& rhs) const { return !operator==(rhs); }

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
      util::ThrowRuntimeError("Unsupported block file version ", version, " in file ",
                              path_.string());

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

template <bool kHeadersOnly = false>
class SerializedBlockReader {
 public:
  SerializedBlockReader(const std::filesystem::path& path, const std::array<uint8_t, 8>& xor_key)
      : path_{path},
        stream_{path, std::ios::binary | std::ios::in},
        xor_key_(xor_key) {
    if (!stream_)
      util::ThrowRuntimeError("Failed to open block file \"", path_.string(), "\" for reading.");
    stream_.seekg(0, std::ios::end);
    file_size_ = stream_.tellg();
    stream_.seekg(0, std::ios::beg);
    Advance();
  }

  using Element = std::conditional_t<kHeadersOnly, std::optional<protocol::BlockHeader>, std::shared_ptr<protocol::Block>>;

  Element ReadNext()  {
    auto rv = std::move(next_);
    if (rv) Advance();
    return rv;
  }

 private:
  void Advance() {
    struct Header {
      uint32_t magic;
      uint32_t size;
    };

    next_.reset();
    if (stream_.tellg() > file_size_ - static_cast<int>(sizeof(Header))) return;

    try {
      int key_pos = stream_.tellg() & 7;
      uint64_t raw = util::Read<uint64_t>(stream_);
      for (unsigned int i = 0; i < sizeof(raw); ++i)
        reinterpret_cast<uint8_t*>(&raw)[i] ^= xor_key_[key_pos++ & 7];
      const Header header = *reinterpret_cast<const Header*>(&raw);
      
      if (header.magic == 0) return;
      if (header.magic != static_cast<uint32_t>(protocol::Magic::Main))
        util::ThrowRuntimeError("Block file does not contain mainnet blocks.");
      if (header.size > 4u << 20)
        util::ThrowRuntimeError("Block size larger than maximum allowed.");
      if (header.size < sizeof(protocol::BlockHeader))
        util::ThrowRuntimeError("Block size smaller than minimum allowed.");

      const int read_size = kHeadersOnly ? sizeof(protocol::BlockHeader) : header.size;
      std::vector<uint8_t> bytes(read_size);
      stream_.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
      if (stream_.gcount() != std::ssize(bytes))
        util::ThrowRuntimeError("Failure reading block file.");

      for (uint8_t& byte : bytes) byte ^= xor_key_[key_pos++ & 7];
      encoding::Reader reader{bytes};

      Element element;
      if constexpr (kHeadersOnly) {
        element = std::make_optional<protocol::BlockHeader>();
      } else {
        element = std::make_shared<protocol::Block>();
      }
      element->Deserialize(reader);
      next_ = std::move(element);
      if (int seek_delta = header.size - read_size; seek_delta != 0)
        stream_.seekg(seek_delta, std::ios::cur);
    } catch (...) {
      // EOF or error, stop advancing
    }
  }

  std::filesystem::path path_;
  mutable std::ifstream stream_;
  const std::array<uint8_t, 8> xor_key_;
  std::streamsize file_size_ = 0;
  Element next_;
};

}  // namespace hornet::data
