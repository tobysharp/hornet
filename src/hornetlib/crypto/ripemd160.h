#pragma once

// RIPEMD-160
// Implemented from spec https://homes.esat.kuleuven.be/~bosselae/ripemd160/pdf/AB-9601/AB-9601.pdf
//

#include <algorithm>
#include <array>
#include <cstdint>

namespace hornet::crypto::RIPEMD160 {

using hash160_t = std::array<uint8_t, 20>;

hash160_t Hash(const unsigned char* byteStream, size_t sizeInBytes);

template <typename Iter>
hash160_t Hash(Iter begin, Iter end);

namespace Detail {
using uint160_t = std::array<uint32_t, 5>;

static constexpr std::array<uint8_t, 80> r = {
    0,  1, 2,  3,  4, 5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15, 7, 4,  13, 1, 10, 6, 15, 3,  12, 0, 9,
    5,  2, 14, 11, 8, 3,  10, 14, 4, 9, 15, 8,  1,  2,  7,  0,  6, 13, 11, 5, 12, 1, 9,  11, 10, 0, 8,
    12, 4, 13, 3,  7, 15, 14, 5,  6, 2, 4,  0,  5,  9,  7,  12, 2, 10, 14, 1, 3,  8, 11, 6,  15, 13};

static constexpr std::array<uint8_t, 80> rp = {
    5,  14, 7, 0,  9, 2,  11, 4, 13, 6,  15, 8,  1,  10, 3, 12, 6, 11, 3, 7, 0,  13, 5, 10, 14, 15, 8,
    12, 4,  9, 1,  2, 15, 5,  1, 3,  7,  14, 6,  9,  11, 8, 12, 2, 10, 0, 4, 13, 8,  6, 4,  1,  3,  11,
    15, 0,  5, 12, 2, 13, 9,  7, 10, 14, 12, 15, 10, 4,  1, 5,  8, 7,  6, 2, 13, 14, 0, 3,  9,  11};

static constexpr std::array<uint8_t, 80> s = {
    11, 14, 15, 12, 5,  8,  7,  9, 11, 13, 14, 15, 6,  7,  9, 8,  7,  6,  8,  13, 11, 9,  7,  15, 7,  12, 15,
    9,  11, 7,  13, 12, 11, 13, 6, 7,  14, 9,  13, 15, 14, 8, 13, 6,  5,  12, 7,  5,  11, 12, 14, 15, 14, 15,
    9,  8,  9,  14, 5,  6,  8,  6, 5,  12, 9,  15, 5,  11, 6, 8,  13, 12, 5,  12, 13, 14, 11, 8,  5,  6};

static constexpr std::array<uint8_t, 80> sp = {
    8, 9,  9,  11, 13, 15, 15, 5,  7,  7, 8, 11, 14, 14, 12, 6, 9,  13, 15, 7,  12, 8,  9,  11, 7,  7,  12,
    7, 6,  15, 13, 11, 9,  7,  15, 11, 8, 6, 6,  14, 12, 13, 5, 14, 13, 13, 7,  5,  15, 5,  8,  11, 14, 14,
    6, 14, 6,  9,  12, 9,  12, 5,  15, 8, 8, 5,  12, 9,  12, 5, 14, 6,  8,  13, 6,  5,  15, 13, 11, 11};

inline uint32_t ReverseEndianWord(uint32_t x) {
  return (x << 24) | ((x & 0x0000FF00) << 8) | ((x & 0x00FF0000) >> 8) | (x >> 24);
}

inline uint32_t K(uint8_t j) {
  static constexpr std::array<uint32_t, 5> Karr = {0x00000000, 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xA953FD4E};
  return Karr[j >> 4];
}

inline uint32_t Kp(uint8_t j) {
  static constexpr std::array<uint32_t, 5> Kparr = {0x50A28BE6, 0x5C4DD124, 0x6D703EF3, 0x7A6D76E9, 0x00000000};
  return Kparr[j >> 4];
}

inline constexpr uint160_t initial() {
  return {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
}

inline uint32_t rol(uint32_t x, uint8_t bits) {
  return (x << bits) | (x >> (32 - bits));
}

inline uint32_t f(uint8_t j, uint32_t x, uint32_t y, uint32_t z) {
  if (j < 16) return x ^ y ^ z;
  else if (j < 32) return (x & y) | (~x & z);
  else if (j < 48) return (x | ~y) ^ z;
  else if (j < 64) return (x & z) | (y & ~z);
  else return x ^ (y | ~z);
}

inline void Process16WordBlock(const uint32_t* X_i, uint160_t& h) {
  auto A = h[0], B = h[1], C = h[2], D = h[3], E = h[4];
  auto Ap = A, Bp = B, Cp = C, Dp = D, Ep = E;
  uint32_t T;
  for (auto j = 0; j < 80; ++j) {
    T = rol(A + f(j, B, C, D) + X_i[r[j]] + K(j), s[j]) + E;
    A = E;
    E = D;
    D = rol(C, 10);
    C = B;
    B = T;
    T = rol(Ap + f(79 - j, Bp, Cp, Dp) + X_i[rp[j]] + Kp(j), sp[j]) + Ep;
    Ap = Ep;
    Ep = Dp;
    Dp = rol(Cp, 10);
    Cp = Bp;
    Bp = T;
  }
  T = h[1] + C + Dp;
  h[1] = h[2] + D + Ep;
  h[2] = h[3] + E + Ap;
  h[3] = h[4] + A + Bp;
  h[4] = h[0] + B + Cp;
  h[0] = T;
}
}  // namespace Detail

inline hash160_t Hash(const unsigned char* byteStream, size_t sizeInBytes) {
  using namespace Detail;

  uint160_t H = initial();
  size_t bytesProcessed = 0;
  constexpr size_t bytesPerBlock = 64;

  // All the full 512-bit blocks can be processed immediately in streaming fashion
  // According to the MD4 spec, we leave the words in their native little-endian format
  for (; sizeInBytes - bytesProcessed >= bytesPerBlock; bytesProcessed += bytesPerBlock)
    Process16WordBlock(reinterpret_cast<const uint32_t*>(byteStream + bytesProcessed), H);

  std::array<uint32_t, 32> localWords = {};  // Initialize blocks to zero bits
  uint8_t* localBytes = reinterpret_cast<uint8_t*>(&localWords[0]);

  // Copy the message data without reversing endian-ness
  std::copy(byteStream + bytesProcessed, byteStream + sizeInBytes, localBytes);

  // Add the one bit after lBits of message data
  const uint32_t bytesRemaining = static_cast<uint32_t>(sizeInBytes - bytesProcessed);
  localBytes[bytesRemaining] = 0x80;

  // Add the message size in bits
  const uint32_t lBits = bytesRemaining << 3;  // l bits of the message remaining
  const uint32_t zeroBits =
      lBits <= 447 ? 447 - lBits : 512 + 447 - lBits;               // k zero bits where l + 1 + k = 448 (mod 512)
  const uint32_t messageSizeWordPos = (lBits + 1 + zeroBits) >> 5;  // The message size goes after the zero padding bits
  const uint64_t messageSizeInBits = sizeInBytes << 3;
  const uint32_t messageSizeLoWord = static_cast<uint32_t>(messageSizeInBits);
  const uint32_t messageSizeHiWord = static_cast<uint32_t>(messageSizeInBits >> 32);
  localWords[messageSizeWordPos] = messageSizeLoWord;
  localWords[messageSizeWordPos + 1] = messageSizeHiWord;

  // Process the remaining blocks
  Process16WordBlock(&localWords[0], H);
  if (lBits >= 448)  // messageSizeWordPos + 1 >= 16
    Process16WordBlock(&localWords[16], H);

  // Return the final hash value
  return reinterpret_cast<const hash160_t&>(H);
}

template <typename Iter>
hash160_t Hash(Iter begin, Iter end) {
  const auto diff = end - begin;
  const size_t bytes = diff * sizeof(std::remove_reference_t<decltype(*begin)>);
  return Hash(bytes == 0 ? nullptr : reinterpret_cast<const unsigned char*>(&*begin), bytes);
}
}  // namespace hornet::crypto::RIPEMD160
