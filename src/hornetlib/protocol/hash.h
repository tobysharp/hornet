// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

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
