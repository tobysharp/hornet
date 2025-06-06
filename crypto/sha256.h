#pragma once

// SHA-256
// Implemented from the spec at
// https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <span>

namespace hornet::crypto {

namespace SHA256 {
using hash256_t = std::array<uint8_t, 32>;

// Compute the SHA-256 hash of an arbitrary byte stream
hash256_t Hash(std::span<const uint8_t> bytes);
}  // namespace SHA256

/* Implementation follows */

namespace SHA256 {
namespace Detail {
using uint256_t = std::array<uint32_t, 8>;
static constexpr uint256_t s_initialHash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
static constexpr std::array<uint32_t, 64> s_K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

using Schedule = std::array<uint32_t, 64>;
using Block = std::array<uint32_t, 16>;  // 512-bit message block

template <uint8_t Count>
inline uint32_t ROTR(uint32_t x) {
  return (x >> Count) | (x << (32 - Count));
}

template <uint8_t Count>
inline uint32_t SHR(uint32_t x) {
  return x >> Count;
}

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t Sigma_0(uint32_t x) {
  return ROTR<2>(x) ^ ROTR<13>(x) ^ ROTR<22>(x);
}

inline uint32_t Sigma_1(uint32_t x) {
  return ROTR<6>(x) ^ ROTR<11>(x) ^ ROTR<25>(x);
}

inline uint32_t sigma_0(uint32_t x) {
  return ROTR<7>(x) ^ ROTR<18>(x) ^ SHR<3>(x);
}

inline uint32_t sigma_1(uint32_t x) {
  return ROTR<17>(x) ^ ROTR<19>(x) ^ SHR<10>(x);
}

inline uint32_t ReverseEndianByteIndex(uint32_t index) {
  return (index & ~3u) | (3u - (index & 3u));
}

inline uint32_t ReverseEndianWord(uint32_t x) {
  return (x << 24u) | ((x & 0x0000FF00) << 8) | ((x & 0x00FF0000) >> 8) | (x >> 24);
}

inline hash256_t ReverseEndianWords(const uint256_t &words) {
  hash256_t rv;
  uint32_t *output = reinterpret_cast<uint32_t *>(&rv[0]);
  for (unsigned int i = 0; i < 8; ++i) output[i] = ReverseEndianWord(words[i]);
  return rv;
}

inline void Process16WordBlock(const uint32_t *M, Schedule &W, uint256_t &H) {
  // Prepare the message schedule {W_t}
  for (uint8_t t = 0; t < 16; ++t) W[t] = M[t];
  for (uint8_t t = 16; t < 64; ++t)
    W[t] = sigma_1(W[t - 2]) + W[t - 7] + sigma_0(W[t - 15]) + W[t - 16];

  // Initialize the working variables a-h with the previous hash value
  auto a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];

  for (uint8_t t = 0; t < 64; ++t) {
    const uint32_t T1 = h + Sigma_1(e) + Ch(e, f, g) + s_K[t] + W[t];
    const uint32_t T2 = Sigma_0(a) + Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;
  }

  // Update the hash value
  H[0] += a;
  H[1] += b;
  H[2] += c;
  H[3] += d;
  H[4] += e;
  H[5] += f;
  H[6] += g;
  H[7] += h;
}
}  // namespace Detail

inline hash256_t Hash(std::span<const uint8_t> bytes) {
  using namespace Detail;

  const uint8_t *const byteStream = bytes.data();
  const size_t sizeInBytes = bytes.size_bytes();
  Schedule W;  // Local state
  uint256_t H = s_initialHash;
  size_t bytesProcessed = 0;
  std::array<uint32_t, 32> localWords;
  constexpr size_t wordsPerBlock = 16;
  constexpr size_t bytesPerBlock = wordsPerBlock * sizeof(uint32_t);

  // All the full 512-bit blocks can be processed immediately in streaming
  // fashion
  while (sizeInBytes - bytesProcessed >= bytesPerBlock) {
    // The SHA-256 spec expects words to be organized in big endian format,
    // so here we convert the byte stream appropriately so that e.g. the byte
    // stream "abcd" will end up as 0x61626364
    const uint32_t *pSrcWords = reinterpret_cast<const uint32_t *>(byteStream + bytesProcessed);
    std::transform(pSrcWords, pSrcWords + wordsPerBlock, &localWords[0],
                   [](uint32_t x) { return ReverseEndianWord(x); });
    Process16WordBlock(&localWords[0], W, H);
    bytesProcessed += bytesPerBlock;
  }

  // Initialize blocks to zero bits
  std::fill(localWords.begin(), localWords.end(), 0);
  uint8_t *localBytes = reinterpret_cast<uint8_t *>(&localWords[0]);
  const uint32_t bytesRemaining = static_cast<uint32_t>(sizeInBytes - bytesProcessed);

  // Copy the message data while reversing endian-ness
  for (uint32_t iByte = 0; iByte < bytesRemaining; ++iByte)
    localBytes[ReverseEndianByteIndex(iByte)] = byteStream[bytesProcessed + iByte];

  // Add the one bit after lBits of message data
  localBytes[ReverseEndianByteIndex(bytesRemaining)] = 0x80;

  // Add the message size in bits
  const uint32_t lBits = bytesRemaining << 3;  // l bits of the message remaining
  const uint32_t zeroBits = lBits <= 447
                                ? 447 - lBits
                                : 512 + 447 - lBits;  // k zero bits where l + 1 + k = 448 (mod 512)
  const uint32_t messageSizeWordPos =
      (lBits + 1 + zeroBits) >> 5;  // The message size goes after the zero padding bits
  const uint64_t messageSizeInBits = sizeInBytes << 3;
  const uint32_t messageSizeLoWord = static_cast<uint32_t>(messageSizeInBits);
  const uint32_t messageSizeHiWord = static_cast<uint32_t>(messageSizeInBits >> 32);
  localWords[messageSizeWordPos] = messageSizeHiWord;
  localWords[messageSizeWordPos + 1] = messageSizeLoWord;

  // Process the remaining blocks
  Process16WordBlock(&localWords[0], W, H);
  if (lBits >= 448)  // messageSizeWordPos + 1 >= 16
    Process16WordBlock(&localWords[16], W, H);

  // Return the final hash value
  return ReverseEndianWords(H);
}

}  // namespace SHA256

}  // namespace hornet::crypto
