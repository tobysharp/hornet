#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace hornet::crypto::sha {

using Bytes20 = std::array<uint8_t, 20>;
using Bytes32 = std::array<uint8_t, 32>;

using Block = std::array<uint32_t, 16>;  // 512-bit message block

template <uint8_t Count> inline uint32_t ROTL(uint32_t x) { return (x << Count) | (x >> (32 - Count)); }
template <uint8_t Count> inline uint32_t ROTR(uint32_t x) { return (x >> Count) | (x << (32 - Count)); }
template <uint8_t Count> inline uint32_t SHR(uint32_t x) { return x >> Count; }
inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t Parity(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }

template <std::integral T> inline T ReverseEndianByteIndex(T index) { return (index & ~3u) | (3u - (index & 3u)); }

template <size_t kWords>
inline std::array<uint8_t, kWords * 4> ReverseEndianWords(const std::array<uint32_t, kWords>& words) {
  std::array<uint8_t, kWords * 4> rv;
  for (size_t i = 0; i < kWords; ++i) {
    rv[i * 4 + 0] = static_cast<uint8_t>(words[i] >> 24);
    rv[i * 4 + 1] = static_cast<uint8_t>(words[i] >> 16);
    rv[i * 4 + 2] = static_cast<uint8_t>(words[i] >> 8);
    rv[i * 4 + 3] = static_cast<uint8_t>(words[i] >> 0);
  }
  return rv;
}

template <typename Processor> inline auto ComputeHash(Processor& p, std::span<const uint8_t> bytes) {
  constexpr size_t kBytesPerBlock = sizeof(Block);

  Block block;
  size_t bytes_processed = 0;

  // All the full 512-bit blocks can be processed immediately in streaming fashion.
  while (bytes.size() - bytes_processed >= kBytesPerBlock) {
    std::memcpy(&block[0], bytes.data() + bytes_processed, sizeof(Block));
    for (uint32_t& word : block) word = std::byteswap(word);
    p(block);
    bytes_processed += kBytesPerBlock;
  }

  block.fill(0);
  uint8_t* block_bytes = reinterpret_cast<uint8_t*>(block.data());
  const auto remaining = bytes.subspan(bytes_processed);

  // Copy the message data while reversing endian-ness
  for (size_t i = 0; i < remaining.size(); ++i) block_bytes[ReverseEndianByteIndex(i)] = remaining[i];

  // Add the one bit after the message data.
  block_bytes[ReverseEndianByteIndex(remaining.size())] = 0x80;

  // If the padding marker consumes the final length field, flush this block
  // and write the length into a fresh zero block.
  if (remaining.size() >= 56) {
    p(block);
    block.fill(0);
  }

  const uint64_t message_size_in_bits = static_cast<uint64_t>(bytes.size()) << 3;
  block[14] = static_cast<uint32_t>(message_size_in_bits >> 32);
  block[15] = static_cast<uint32_t>(message_size_in_bits);
  p(block);
}

}  // namespace hornet::crypto::sha
