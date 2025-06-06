#pragma once

#include "crypto/hash.h"
#include "util/big_uint.h"

namespace hornet::protocol {

// Represents a 256-bit hash, as a 32-byte array in little-endian order.
using Hash = crypto::bytes32_t;

inline constexpr Hash kGenesisHash =
    util::ParseHex32("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");

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
