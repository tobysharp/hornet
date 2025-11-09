// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cassert>
#include <compare>
#include <cstring>
#include <iomanip>
#include <ios>
#include <ostream>

// Proof-of-work types and relationships:
//
// Hash -> compare leq -> Target <- .Expand() <- CompactTarget
//                          |
//                      .GetWork()
//                          |
//                         Work
//

namespace hornet::protocol {

// Represents a 256-bit hash, as a 32-byte array in little-endian order.
struct Hash : public std::array<uint8_t, 32> {
  using Base = std::array<uint8_t, 32>;
  constexpr Hash() = default;
  constexpr Hash(std::array<uint8_t, 32> x) : Base{std::move(x)} {}
  constexpr Hash(std::initializer_list<uint8_t> x) {
    assert(x.size() <= 32);
    auto it = x.begin();
    for (size_t i = 0; i < x.size(); ++i) (*this)[i] = *it++;
    for (size_t i = x.size(); i < 32; ++i) (*this)[i] = 0;
  }
  bool IsNull() const {
    return *this == Hash{};
  }
  explicit operator bool() const { return !IsNull(); }
  std::strong_ordering operator<=>(const Hash& b) const {
    return std::memcmp(data(), b.data(), sizeof(Hash)) <=> 0;
  }
  friend std::ostream& operator <<(std::ostream& os, const protocol::Hash& hash) {
    os << "\"";
    for (int i = sizeof(hash) - 1; i >= 0; --i)
      os << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    os << "\"";
    return os;
  }
};

}  // namespace hornet::protocol

namespace std {

template <>
struct hash<hornet::protocol::Hash> {
  size_t operator()(const hornet::protocol::Hash& h) const noexcept {
    static_assert(sizeof(hornet::protocol::Hash) == 32);
    size_t result;
    std::memcpy(&result, h.data(), sizeof(result));
    return result;
  }
};

}  // namespace std
