#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "hornetlib/data/utxo/search.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/throw.h"

/*
Compact Index
=============

The compact index is an important optimization in the search for unspent outputs.

With ~173M UTXOs, and 36 bytes per UTXO, that's 5.8 GB of *key* data to be searched for each block,
before even considering at least probably another 1.2 GB of offset values just to be able to go and
look up the output data in a file.

But 173M fits in T=ceil(log_2(173e6))=28 bits, so some kind of compression is possible. Here's the setup:

The full index of ({txid, vout}, offset) values is 44 bytes (48 with alignment).
We consider the key to be the 36-byte {txid, vout} in big-endian format, so that
the leading bits are the most significant bits of txid.

This full index is partitioned into shards for parallelism, where we have a number of shards
2^S similar to the number of logical processors (e.g. S=3,4). Each shard has its own portion of the
index, determined by S leading bits of the key.

We augment the shard with a directory, which is an array of size 2^D+1 storing 32-bit integers (e.g. D=8).
Now for each d in [0, 2^D), the d^th slice of the index is the subarray of index entries where bits
S:S+D-1 of the key are equal to d. The directory entry at d stores the starting position of this slice.
For e.g. D=8, the size of the directory is 2^8 * 4 = 1 KiB, and across 8 shards costs only 8 KiB of memory.

We create a compact index which has a 1:1 mapping of entries with the full index, but applies a custom
compression scheme that preserves a weak ordering on the lexicographic order of the keys.

Our goal with the compact index is to create a much smaller key/value representation that can be used to 
accelerate a query reliably, reducing memory bandwidth and memory allocation. The idea is to query against
the compact index first, generating candidates for possible matches, and then to validate those candidate
positions in the full index and retrieve their offsets into the cold store.

How many bits would we need to store a position into the full index? With 28 bits for all 173M UTXOs, we would
need 28-S bits for the per-shard index. But for each query we already know the slice start position for that
query key, which is the earliest possible match position in the index. So we don't need to store these D bits
either, since we can store positions relative to an entry's slice. We're now down to needing P=T-S-D bits for
the position within the full index. Suppose T=28, S=4, D=9. Then we need to store at least P=28-4-9=15 bits for 
the position in a compact index.

Suppose we use P=16 bits to store the position value in a compact index. If we use a 32-bit value for the whole
key/value pair, then this leaves us with K=32-P=16 bits for the key. 

We already know S+D bits of the key implicitly via the shard and directory structure. So for the compact key,
we take bits S+D:S+D+K-1 of the full key. Equivalently, we take the next K bits from the high word of txid.

Now in our 32-bit key/value entry, we have S+D+K bits of effective entropy for the key, and S+D+P effective
bits for the position. This gives us a false positive rate for matching candidates of ~173M/2^29 = ~0.32, and
means that we can index into up to 537M entries with a single 4-byte integer.

The total memory use for these 4-byte compressed key/value pairs is 173M * 4 bytes = 0.64GB.

*/
namespace hornet::data {

class CompactIndex {
 public:
  CompactIndex(int skip_bits) : skip_bits_(skip_bits) {
    Assert(CompactKeyValue::kKeyBits < 32 - skip_bits_);
  }

  template <typename Visit>
  int Query(std::span<const protocol::OutPoint> queries,
                      const int lo, const int hi,
                      Visit&& visit) const {
    const auto key = [&](const protocol::OutPoint& op) -> uint16_t {
      return KeyPrefix(op);
    };
    const auto lower = compact_.begin() + std::max(lo, 0);
    const auto upper = hi < std::ssize(compact_) ? compact_.begin() + hi : compact_.end();
    return ForEachMatchInDoubleSorted(queries.begin(), queries.end(), lower, upper, key, std::forward<Visit>(visit));
  }

  // The key invariant we must preserve with the prefix is weak ordering.
  // Specifically, for keys k_i and prefixes p_i, we must have:
  // 1. k_1 < k2 => p_1 <= p_2;
  // 2. k_1 = k2 => p_1  = p_2;
  uint16_t KeyPrefix(const protocol::OutPoint& out_point) const {
    uint32_t word;
    // Take 32 bits of entropy from txid.
    std::memcpy(&word, out_point.hash.data(), sizeof(word));
    // Strip the high-order bits we already used for the shard index and the directory index (~16).
    uint32_t after = word >> skip_bits_;
    // Mask out the bits that aren't within our prefix window.
    return after & ((1 << CompactKeyValue::kKeyBits) - 1);
  }

   class CompactKeyValue {
   public:
    CompactKeyValue(uint16_t key, uint32_t value)
        : storage_((key << kKeyBits) + (value & kValueMask)) {}
    CompactKeyValue(uint32_t storage) : storage_(storage) {}

    uint16_t Key() const {
      return static_cast<uint16_t>(storage_ >> kValueBits);
    }
    uint32_t Value() const {
      return storage_ & kValueMask;
    }
    uint32_t Storage() const {
      return storage_;
    }
    friend std::strong_ordering operator<=>(CompactKeyValue lhs, uint16_t rhs) {
      return lhs.Key() <=> rhs;
    }
    friend std::strong_ordering operator<=>(uint16_t lhs, CompactKeyValue rhs) {
      return lhs <=> rhs.Key();
    }
    static uint16_t MaximumKey() {
      return static_cast<uint16_t>((1 << kKeyBits) - 1);
    }
    static uint32_t MaximumValue() {
      return (1 << kValueBits) - 1;
    }

    static constexpr int kValueBits = 19;
    static constexpr int kKeyBits = 32 - kValueBits;
   private:
    static constexpr int kValueMask = (1 << kValueBits) - 1;
    uint32_t storage_;
  };

 private:
  const int skip_bits_;
  std::vector<CompactKeyValue> compact_;
};

}  // namespace hornet::data
