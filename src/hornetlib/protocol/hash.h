// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstring>
#include <ostream>

#include "hornetlib/crypto/hash.h"
#include "hornetlib/util/hex.h"

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
using Hash = crypto::bytes32_t;

}  // namespace hornet::protocol

namespace hornet {

inline std::ostream& operator<<(std::ostream& os, const protocol::Hash& hash) {
  os << "\"";
  for (int i = sizeof(hash) - 1; i >= 0; --i)
    os << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
  os << "\"";
  return os;
}

}  // namespace hornet


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
