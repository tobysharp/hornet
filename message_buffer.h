#pragma once

#include "types.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

// A buffer for serializing Bitcoin protocol messages.
class MessageBuffer {
 public:
  size_t Size() const {
    return data_.size();
  }
  
  // Adds raw bytes to the buffer.
 void Add(std::span<const uint8_t> bytes) {
    data_.insert(data_.end(), bytes.begin(), bytes.end());
  }

  // Adds a string to the buffer, prefixed with a variable-int length.
  void Add(const std::string& s) {
    AddVarInt(s.size());
    data_.insert(data_.end(), s.begin(), s.end());
  }

  // Adds raw bytes in reverse order.
  void AddReverse(std::span<const uint8_t> bytes) {
    data_.insert(data_.end(), bytes.rbegin(), bytes.rend());
  }

  // Adds an 8-bit unsigned integer.
  size_t Add(uint8_t value) {
    size_t index = Size();
    data_.push_back(value);
    return index;
  }

  // Adds a boolean as an 8-bit integer (1 or 0).
  size_t Add(bool value) {
    return Add(static_cast<uint8_t>(value ? 1 : 0));
  }

  // Adds a 16-bit little-endian integer.
  size_t Add(uint16_t value) {
    return AddLittleEndian(value);
  }

  // Adds a 16-bit big-endian integer.
  size_t AddBigEndian(uint16_t value) {
    return AddBigEndian<uint16_t>(value);
  }

  // Adds a 32-bit little-endian unsigned integer.
  size_t Add(uint32_t value) {
    return AddLittleEndian(value);
  }

  // Adds a 32-bit little-endian signed integer.
  size_t Add(int32_t value) {
    return AddLittleEndian(value);
  }

  // Adds a 64-bit little-endian integer.
  size_t Add(uint64_t value) {
    return AddLittleEndian(value);
  }

  // Adds a variable-length integer using Bitcoin's VarInt encoding.
  // Format:
  //   value < 0xfd          -> 1 byte
  //   <= 0xffff             -> 0xfd followed by 2 bytes
  //   <= 0xffffffff         -> 0xfe followed by 4 bytes
  //   otherwise             -> 0xff followed by 8 bytes
  void AddVarInt(uint64_t value) {
    if (value < 0xfd) {
      Add(static_cast<uint8_t>(value));
    } else if (value <= 0xffff) {
      Add(uint8_t{0xfd});
      Add(static_cast<uint16_t>(value));
    } else if (value <= 0xffffffff) {
      Add(uint8_t{0xfe});
      Add(static_cast<uint32_t>(value));
    } else {
      Add(uint8_t{0xff});
      Add(value);
    }
  }

  void WriteAt(size_t index, std::span<const uint8_t> bytes) {
    std::copy_n(bytes.begin(), bytes.size(), data_.begin() + index);
  }
  
  void WriteAt(size_t index, uint32_t value) {
    auto bytes = AsByteSpan<uint32_t>({&value, 1});
    std::copy_n(bytes.begin(), bytes.size(), data_.begin() + index);
  }
  
  void Clear() {
    data_.clear();
  }

  // Returns a read-only view of the internal data, not a copy.
  std::span<const uint8_t> AsBytes() const {
    return data_;
  }

 private:
  // Add data in little-endian format, i.e. matching the memory layout.
  template <typename T> size_t AddLittleEndian(T value) {
    static_assert(std::is_trivially_copyable_v<T>, "AddLittleEndian requires trivially copyable types.");
    size_t index = Size();
    const auto ptr = static_cast<const uint8_t*>(static_cast<const void*>(&value));
    Add({ptr, sizeof(T)});
    return index;
  }

  // Add data in big-endian format, i.e. reversing the memory layout.
  template <typename T> size_t AddBigEndian(T value) {
    static_assert(std::is_trivially_copyable_v<T>, "AddBigEndian requires trivially copyable types.");
    size_t index = Size();
    const auto ptr = static_cast<const uint8_t*>(static_cast<const void*>(&value));
    AddReverse({ptr, sizeof(T)});
    return index;
  }

  std::vector<uint8_t> data_;
};

// Stream insertion operator for convenience.
template <typename T>
MessageBuffer& operator<<(MessageBuffer& buffer, const T& value) {
  buffer.Add(value);
  return buffer;
}

template <typename T, std::size_t N>
MessageBuffer& operator<<(MessageBuffer& buffer, const std::array<T, N>& arr) {
  static_assert(std::is_same_v<T, uint8_t>, "Only arrays of uint8_t are supported.");
  return buffer << std::span<const uint8_t>(arr.data(), arr.size());
}

inline MessageBuffer& operator<<(MessageBuffer& buffer, std::span<const uint8_t> bytes) {
  buffer.Add(bytes);
  return buffer;
}

// Wrapper for big-endian encoding
template <typename T> struct BigEndian {
  explicit BigEndian(T value) : value_(value) {}
  T value_;
};

template <typename T> inline MessageBuffer& operator<<(MessageBuffer& buffer, const BigEndian<T>& be) {
  buffer.AddBigEndian(be.value_);
  return buffer;
}
