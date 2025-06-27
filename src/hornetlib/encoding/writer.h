// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "hornetlib/encoding/endian.h"

namespace hornet::encoding {

class Writer {
 public:
  Writer() : pos_(buffer_.end()) {}

  // Write raw byte
  size_t WriteByte(uint8_t byte) {
    size_t before = GetPos();

    if (pos_ == buffer_.end()) {
      buffer_.push_back(byte);
      pos_ = buffer_.end();
    } else {
      *pos_++ = byte;
    }
    return before;
  }

  size_t WriteBool(bool flag) {
    return WriteByte(static_cast<uint8_t>(flag ? 1 : 0));
  }

  // Write raw bytes
  size_t WriteBytes(std::span<const uint8_t> span) {
    size_t offset = GetPos();

    if (pos_ == buffer_.end()) {
      // Append to the end of the buffer
      buffer_.insert(pos_, span.begin(), span.end());
      pos_ = buffer_.end();
    } else {
      // Overwrite the current data
      auto remaining = std::distance(pos_, buffer_.end());
      size_t to_write = std::min<size_t>(remaining, span.size());
      std::copy_n(span.begin(), to_write, pos_);
      pos_ += to_write;
      // Appends the remaining data to the buffer end
      if (to_write < span.size()) {
        buffer_.insert(pos_, span.begin() + to_write, span.end());
        pos_ = buffer_.end();
      }
    }
    return offset;
  }

  template <std::integral T>
  size_t WriteRaw(T value) {
    return WriteBytes({reinterpret_cast<const uint8_t *>(&value), sizeof(T)});
  }

  // Writes an integral value in little-endian order
  template <std::integral T>
  size_t WriteLE(T value) {
    return WriteRaw(NativeToLittleEndian(value));
  }

  // Write an integral value as 2-bytes little-endian
  template <std::integral T>
  size_t WriteLE2(T value) {
    return WriteLE(NarrowOrThrow<uint16_t>(value));
  }

  // Write an integral value as 4-bytes little-endian
  template <std::integral T>
  size_t WriteLE4(T value) {
    return WriteLE(NarrowOrThrow<uint32_t>(value));
  }

  // Write an integral value as 8-bytes little-endian
  template <std::integral T>
  size_t WriteLE8(T value) {
    return WriteLE(NarrowOrThrow<uint64_t>(value));
  }

  // Writes an integral value in big-endian order
  template <std::integral T>
  size_t WriteBE(T value) {
    return WriteRaw(NativeToBigEndian(value));
  }

  // Write an integral value as 2-bytes big-endian
  template <std::integral T>
  size_t WriteBE2(T value) {
    return WriteBE(NarrowOrThrow<uint16_t>(value));
  }

  // Writes a variable-length unsigned integer
  template <std::unsigned_integral T>
  size_t WriteVarInt(T value) {
    size_t pos = GetPos();
    if (value < 0xFD) {
      WriteByte(static_cast<uint8_t>(value));
    } else if (value <= 0xFFFF) {
      WriteByte(0xFD);
      WriteLE(static_cast<uint16_t>(value));
    } else if (value <= 0xFFFFFFFF) {
      WriteByte(0xFE);
      WriteLE(static_cast<uint32_t>(value));
    } else {
      WriteByte(0xFF);
      WriteLE(static_cast<uint64_t>(value));
    }
    return pos;
  }

  // Writes a variable-length string
  size_t WriteVarString(const std::string &s) {
    size_t pos = WriteVarInt(s.size());
    WriteBytes({reinterpret_cast<const uint8_t *>(s.data()), s.size()});
    return pos;
  }

  // Writes a string as a fixed-length zero-padded character array.
  template <size_t kLength>
  size_t WriteZeroPaddedString(const std::string &str) {
    if (str.size() > kLength) {
      // NB: String will be silently truncated. Optionally log.
    }
    std::array<char, kLength> cstr = {};
    std::copy_n(str.data(), std::min(str.size(), kLength), cstr.begin());
    return WriteBytes({reinterpret_cast<const uint8_t*>(cstr.data()), sizeof(cstr)});
  }

  // Returns the current seek position
  size_t GetPos() const {
    return static_cast<size_t>(std::distance(const_cast<Writer *>(this)->buffer_.begin(), pos_));
  }

  // Returns true when the seek position is at the end of the buffer
  bool IsEOF() const {
    return pos_ == buffer_.end();
  }

  // Sets the internal seek position to allow overwriting
  size_t SeekPos(size_t offset) {
    const size_t old = GetPos();
    pos_ = buffer_.begin() + static_cast<intptr_t>(offset);
    return old;
  }

  // Buffer access
  const std::vector<uint8_t>& Buffer() const {
    return buffer_;
  }

  std::vector<uint8_t> ReleaseBuffer() {
    return std::move(buffer_);
  }

  // Clears and resets the internal buffer and seek position
  void Clear() {
    buffer_.clear();
    pos_ = buffer_.end();
  }

 private:
  std::vector<uint8_t> buffer_;
  std::vector<uint8_t>::iterator pos_;
};

}  // namespace hornet::encoding
