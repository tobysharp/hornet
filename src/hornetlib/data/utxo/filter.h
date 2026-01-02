#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {

// A blocked Bloom filter to fail fast on negative queries.
class Filter {
 public:
  Filter() = default;

  bool MayContain(const OutputKey& key) const {
    if (blocks_.empty()) return false;
    const auto hash = HashKey(key);
    const Block& block = blocks_[BlockIndex(hash)];
    return ForEachProbe(hash, [&](int word_index, int shift_bits) {
      return (block[word_index] & (1ull << shift_bits)) != 0;
    });
  }

  // Resizes and clears the filter to accommodate the specified number of entries.
  void Reset(int size) {
    const auto num_blocks = (static_cast<size_t>(size) * kBitsPerEntry + kBlockSizeBits - 1) / kBlockSizeBits;
    blocks_.assign(num_blocks, {});
  }

  // Adds a key to the filter. Must call Reset first.
  void Add(const OutputKey& key) {
    if (blocks_.empty()) return;
    const auto hash = HashKey(key);
    Block& block = blocks_[BlockIndex(hash)];
    ForEachProbe(hash, [&](int word_index, int shift_bits) {
      block[word_index] |= (1ull << shift_bits);
      return true;
    });
  }

 private:
  static constexpr int kBitsPerEntry = 12;  // A memory/accuracy trade-off that gives 1% false positive rate.
  static constexpr int kBlockSizeBits = 512;  // Size of typical cache line (64 bytes).
  static constexpr int kBlockSizeWords = kBlockSizeBits / 64;
  static constexpr int kProbes = 7;  // static_cast<int>(kBitsPerEntry * std::log(2.0));
  static constexpr uint64_t kGoldenRatioMixer = 0x9e3779b97f4a7c15ull;  // The fixed-point fractional part of the golden ratio.

  struct alignas(64) Block : public std::array<uint64_t, kBlockSizeWords> {};

  struct Hash {
    uint32_t block_pattern;  // Determines which block is selected.
    uint64_t bit_pattern;    // Determines which bits are selected from the block.
  };

  uint32_t BlockIndex(const Hash& hash) const {
    // Maps the range [0, 2^32-1] -> [0, blocks_.size()-1].
    return (uint64_t{hash.block_pattern} * blocks_.size()) >> 32;
  }

  static bool ForEachProbe(const Hash& hash, auto&& func) {
    uint64_t h = hash.bit_pattern;
    for (int i = 0; i < kProbes; ++i) {
      const int bit_index = h & (kBlockSizeBits - 1);
      const int word_index = bit_index >> 6;
      const int shift_bits = bit_index & 63;
      if (!func(word_index, shift_bits)) return false;
      h >>= 9;
    }
    return true;
  }

  // Generate a fast hash of the key for block and bit selection.
  static Hash HashKey(const OutputKey& key) {
    uint32_t block;
    uint64_t bits;
    std::memcpy(&block, key.hash.data(), sizeof(block));
    std::memcpy(&bits, key.hash.data() + sizeof(block), sizeof(bits));

    static constexpr uint64_t kIndexMixer = 0x9e3779b97f4a7c15ULL;
    return {block, bits ^ (static_cast<uint64_t>(key.index) * kIndexMixer)};
  }

  std::vector<Block> blocks_;
};

}  // namespace hornet::data::utxo
