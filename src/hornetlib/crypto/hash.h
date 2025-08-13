// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <span>

#include "hornetlib/crypto/sha256.h"
#include "hornetlib/util/as_span.h"

namespace hornet::crypto {

using bytes32_t = std::array<uint8_t, 32>;

template <typename T>
inline constexpr bool always_false_v = false;

// Iterator-based overload (useful for containers)
template <typename Iter>
bytes32_t Sha256(Iter begin, Iter end) {
  using T = std::remove_reference_t<decltype(*begin)>;
  static_assert(std::is_trivially_copyable_v<T>,
                "Sha256: iterator value type must be trivially copyable.");
  const size_t count = static_cast<size_t>(end - begin);
  return SHA256::Hash(util::AsByteSpan<T>({count > 0 ? &*begin : nullptr, count}));
}

// Overload for trivially copyable types, spans, and ranges.
// Hashes trivially copyable objects or range-like containers by treating
// their contents as raw bytes. Not suitable for types with internal pointers or
// padding.
template <typename T>
inline bytes32_t Sha256(const T &value) {
  if constexpr (requires {
                  value.begin();
                  value.end();
                }) {
    return Sha256(value.begin(), value.end());
  } else if constexpr (std::is_trivially_copyable_v<T>) {
    return SHA256::Hash(util::AsByteSpan<T>({&value, 1}));
  } else {
    static_assert(always_false_v<T>, "Unsupported type passed to Sha256.");
  }
}

template <typename Iter>
bytes32_t DoubleSha256(Iter begin, Iter end) {
  return Sha256(Sha256(begin, end));
}

// Overload for trivially copyable types, spans, and ranges
template <typename T>
inline bytes32_t DoubleSha256(const T &value) {
  return Sha256(Sha256(value));
}

// Computes a batch of same-sized double-SHA256 hashes using vectorization.
inline void DoubleSha256Batch(const uint8_t* input,
                              int buffer_length_bytes,
                              int input_stride_bytes,
                              const int buffer_count,
                              uint8_t* output,
                              int output_stride_bytes = 32) {
  // TODO: Replace this scalar implementation with a vectorized one.
  for (int i = 0; i < buffer_count; ++i) {
    const uint8_t* buffer = input + i * input_stride_bytes;
    *reinterpret_cast<bytes32_t*>(output + i * output_stride_bytes) = DoubleSha256(buffer, buffer + buffer_length_bytes);
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
