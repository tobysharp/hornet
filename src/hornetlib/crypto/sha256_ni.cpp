// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

// Compile this file with: -msha -msse4.1
// These flags enable the Intel SHA Extensions intrinsics

#include "sha256_ni.h"

#include <cstring>
#include <immintrin.h>

namespace hornet::crypto::SHA256 {

namespace {

// SHA256 constants K
alignas(16) static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Initial hash values H0
alignas(16) static const uint32_t H256[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

// Byte swap for big-endian conversion
inline uint32_t bswap32(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap32(x);
#elif defined(_MSC_VER)
  return _byteswap_ulong(x);
#else
  return ((x << 24) & 0xff000000) |
         ((x << 8)  & 0x00ff0000) |
         ((x >> 8)  & 0x0000ff00) |
         ((x >> 24) & 0x000000ff);
#endif
}

// Process a single 64-byte (512-bit) block using SHA-NI
inline void SHA256_ProcessBlock_SHANI(__m128i& state0, __m128i& state1, const uint8_t* data) {
  __m128i msg0, msg1, msg2, msg3;
  __m128i tmp;
  __m128i state0_save, state1_save;

  // Save current state
  state0_save = state0;
  state1_save = state1;

  // Load message data (16 x 32-bit words = 64 bytes)
  // SHA-NI expects data in big-endian, so we need to byte-swap
  const uint32_t* data32 = reinterpret_cast<const uint32_t*>(data);
  msg0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data32 + 0));
  msg1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data32 + 4));
  msg2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data32 + 8));
  msg3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data32 + 12));

  // Byte swap to big-endian
  const __m128i shuf_mask = _mm_set_epi64x(0x0c0d0e0f08090a0b, 0x0405060700010203);
  msg0 = _mm_shuffle_epi8(msg0, shuf_mask);
  msg1 = _mm_shuffle_epi8(msg1, shuf_mask);
  msg2 = _mm_shuffle_epi8(msg2, shuf_mask);
  msg3 = _mm_shuffle_epi8(msg3, shuf_mask);

  // Rounds 0-3
  tmp = _mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 0)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_shuffle_epi32(tmp, 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

  // Rounds 4-7
  tmp = _mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 4)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_shuffle_epi32(tmp, 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg0 = _mm_sha256msg1_epu32(msg0, msg1);

  // Rounds 8-11
  tmp = _mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 8)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_shuffle_epi32(tmp, 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg1 = _mm_sha256msg1_epu32(msg1, msg2);

  // Rounds 12-15
  tmp = _mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 12)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg3, msg2, 4);
  msg0 = _mm_add_epi32(msg0, tmp);
  msg0 = _mm_sha256msg2_epu32(msg0, msg3);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 12))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg2 = _mm_sha256msg1_epu32(msg2, msg3);

  // Rounds 16-19
  tmp = _mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 16)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg0, msg3, 4);
  msg1 = _mm_add_epi32(msg1, tmp);
  msg1 = _mm_sha256msg2_epu32(msg1, msg0);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 16))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg3 = _mm_sha256msg1_epu32(msg3, msg0);

  // Rounds 20-23
  tmp = _mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 20)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg1, msg0, 4);
  msg2 = _mm_add_epi32(msg2, tmp);
  msg2 = _mm_sha256msg2_epu32(msg2, msg1);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 20))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg0 = _mm_sha256msg1_epu32(msg0, msg1);

  // Rounds 24-27
  tmp = _mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 24)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg2, msg1, 4);
  msg3 = _mm_add_epi32(msg3, tmp);
  msg3 = _mm_sha256msg2_epu32(msg3, msg2);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 24))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg1 = _mm_sha256msg1_epu32(msg1, msg2);

  // Rounds 28-31
  tmp = _mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 28)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg3, msg2, 4);
  msg0 = _mm_add_epi32(msg0, tmp);
  msg0 = _mm_sha256msg2_epu32(msg0, msg3);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 28))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg2 = _mm_sha256msg1_epu32(msg2, msg3);

  // Rounds 32-35
  tmp = _mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 32)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg0, msg3, 4);
  msg1 = _mm_add_epi32(msg1, tmp);
  msg1 = _mm_sha256msg2_epu32(msg1, msg0);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 32))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg3 = _mm_sha256msg1_epu32(msg3, msg0);

  // Rounds 36-39
  tmp = _mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 36)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg1, msg0, 4);
  msg2 = _mm_add_epi32(msg2, tmp);
  msg2 = _mm_sha256msg2_epu32(msg2, msg1);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 36))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg0 = _mm_sha256msg1_epu32(msg0, msg1);

  // Rounds 40-43
  tmp = _mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 40)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg2, msg1, 4);
  msg3 = _mm_add_epi32(msg3, tmp);
  msg3 = _mm_sha256msg2_epu32(msg3, msg2);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 40))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg1 = _mm_sha256msg1_epu32(msg1, msg2);

  // Rounds 44-47
  tmp = _mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 44)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg3, msg2, 4);
  msg0 = _mm_add_epi32(msg0, tmp);
  msg0 = _mm_sha256msg2_epu32(msg0, msg3);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 44))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg2 = _mm_sha256msg1_epu32(msg2, msg3);

  // Rounds 48-51
  tmp = _mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 48)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg0, msg3, 4);
  msg1 = _mm_add_epi32(msg1, tmp);
  msg1 = _mm_sha256msg2_epu32(msg1, msg0);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg0, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 48))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
  msg3 = _mm_sha256msg1_epu32(msg3, msg0);

  // Rounds 52-55
  tmp = _mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 52)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg1, msg0, 4);
  msg2 = _mm_add_epi32(msg2, tmp);
  msg2 = _mm_sha256msg2_epu32(msg2, msg1);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg1, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 52))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

  // Rounds 56-59
  tmp = _mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 56)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_alignr_epi8(msg2, msg1, 4);
  msg3 = _mm_add_epi32(msg3, tmp);
  msg3 = _mm_sha256msg2_epu32(msg3, msg2);
  tmp = _mm_shuffle_epi32(_mm_add_epi32(msg2, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 56))), 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

  // Rounds 60-63
  tmp = _mm_add_epi32(msg3, _mm_load_si128(reinterpret_cast<const __m128i*>(K256 + 60)));
  state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
  tmp = _mm_shuffle_epi32(tmp, 0x0E);
  state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

  // Add back to state
  state0 = _mm_add_epi32(state0, state0_save);
  state1 = _mm_add_epi32(state1, state1_save);
}

}  // anonymous namespace

hash256_t Hash_SHANI(std::span<const uint8_t> data) {
  // Initialize state with H0
  // Load initial hash values
  __m128i state0, state1;
  __m128i tmp;

  tmp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(H256 + 0));     // Load A, B, C, D
  state1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(H256 + 4));  // Load E, F, G, H

  // Shuffle into SHA-NI format: state0 = {A, B, E, F}, state1 = {C, D, G, H}
  tmp = _mm_shuffle_epi32(tmp, 0xB1);          // CDAB
  state1 = _mm_shuffle_epi32(state1, 0x1B);    // EFGH -> HGFE
  state0 = _mm_alignr_epi8(tmp, state1, 8);    // ABEF
  state1 = _mm_blend_epi16(state1, tmp, 0xF0); // CDGH

  // Process full 64-byte blocks
  size_t num_blocks = data.size() / 64;
  for (size_t i = 0; i < num_blocks; ++i) {
    SHA256_ProcessBlock_SHANI(state0, state1, data.data() + i * 64);
  }

  // Handle padding for final block(s)
  const size_t bytes_processed = num_blocks * 64;
  const size_t remaining = data.size() - bytes_processed;

  alignas(16) uint8_t final_block[128] = {0};
  if (remaining > 0) {
    std::memcpy(final_block, data.data() + bytes_processed, remaining);
  }

  // Add padding bit
  final_block[remaining] = 0x80;

  // Add length in bits as 64-bit big-endian integer at the end
  const uint64_t bit_length = data.size() * 8;
  const size_t length_pos = (remaining < 56) ? 56 : 120;

  // Write length as big-endian
  for (int i = 0; i < 8; ++i) {
    final_block[length_pos + i] = static_cast<uint8_t>(bit_length >> (56 - i * 8));
  }

  // Process final block(s)
  SHA256_ProcessBlock_SHANI(state0, state1, final_block);
  if (remaining >= 56) {
    SHA256_ProcessBlock_SHANI(state0, state1, final_block + 64);
  }

  // Unshuffle state back to standard order (following reference implementation)
  tmp = _mm_shuffle_epi32(state0, 0x1B);       // FEBA
  state1 = _mm_shuffle_epi32(state1, 0xB1);    // DCHG
  state0 = _mm_blend_epi16(tmp, state1, 0xF0);  // DCBA
  state1 = _mm_alignr_epi8(state1, tmp, 8);    // HGFE

  // Store and convert to result format
  hash256_t result;
  _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data() + 0), state0);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data() + 16), state1);

  // The result now contains the hash in byte order, but needs byte swap for big-endian
  uint32_t* words = reinterpret_cast<uint32_t*>(result.data());
  for (int i = 0; i < 8; ++i) {
    words[i] = bswap32(words[i]);
  }

  return result;
}

}  // namespace hornet::crypto::SHA256
