// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <span>

#include "hornetlib/crypto/cpuinfo.h"
#include "hornetlib/crypto/ripemd160.h"
#include "hornetlib/crypto/sha1.h"
#include "hornetlib/crypto/sha256.h"
#include "hornetlib/crypto/sha256_ni.h"
#include "hornetlib/util/as_span.h"
#include "hornetlib/util/throw.h"
namespace hornet::crypto {

using bytes20_t = std::array<uint8_t, 20>;
using bytes32_t = std::array<uint8_t, 32>;

inline bytes32_t Sha256(std::span<const uint8_t> bytes) {
#if defined(HORNET_HAS_SHA_NI)
  if (HasSHAExtensions())
    return SHA256::Hash_SHANI(bytes);
#endif
  return sha::ComputeSHA256(bytes);
}

inline bytes32_t Hash256(std::span<const uint8_t> bytes) {
  const auto sha = Sha256(bytes);
  return Sha256(sha);
}

inline bytes20_t Sha1(std::span<const uint8_t> bytes) {
  return sha::ComputeSHA1(bytes);
}

inline bytes20_t Ripemd160(std::span<const uint8_t> bytes) {
  return RIPEMD160::Hash(bytes.begin(), bytes.end());
}

inline bytes20_t Hash160(std::span<const uint8_t> bytes) {
  const auto sha = Sha256(bytes);
  return RIPEMD160::Hash(sha.begin(), sha.end());
}

// Computes the hash256 of the concatenation of arguments.
// The caller guarantees that N bytes is enough to store the concatenated arguments.
template <std::size_t N, typename... Args>
bytes32_t Hash256Concat(const Args&... args) {
  std::array<uint8_t, N> buffer;
  uint8_t* dst = buffer.data();
  const uint8_t* end = buffer.end();

  const auto append_arg = [&](const auto& x) {
    const auto bytes = util::AsByteSpan(x);
    if (dst + bytes.size() > end) util::ThrowOutOfRange("Hash256Concat buffer overrun");
    dst = std::copy(bytes.begin(), bytes.end(), dst);
  };

  (append_arg(args), ...);
  return Hash256(buffer);
}

// Computes a batch of same-sized hash256 values using vectorization.
inline void Hash256Batch(const uint8_t* input,
                         int buffer_length_bytes,
                         int input_stride_bytes,
                         const int buffer_count,
                         uint8_t* output,
                         int output_stride_bytes = 32) {
  // Use SHA-NI for batch operations
  for (int i = 0; i < buffer_count; ++i) {
    const uint8_t* buffer = input + i * input_stride_bytes;
    const auto hash = Hash256({buffer, static_cast<size_t>(buffer_length_bytes)});
    *reinterpret_cast<bytes32_t*>(output + i * output_stride_bytes) = hash;
  }
}

// Writes the uint256_t as a 64-character hex string to an output stream,
// using big-endian byte order (as typically displayed in Bitcoin).
// Note: this is a textual representation, not binary output.
// Use std::ostream::write or serialization methods for raw binary encoding.
inline std::ostream& operator<<(std::ostream& os, const bytes32_t& h) {
  std::ios_base::fmtflags f(os.flags());  // Save stream flags
  os << std::hex << std::setfill('0');

  for (const uint8_t uc : h) os << std::setw(2) << static_cast<int>(uc);

  os.flags(f);  // Restore flags
  return os;
}

}  // namespace hornet::crypto
