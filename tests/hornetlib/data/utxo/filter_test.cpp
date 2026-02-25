#include "hornetlib/data/utxo/filter.h"

#include <gtest/gtest.h>
#include <random>
#include <vector>

#include "hornetlib/data/utxo/types.h"

namespace hornet::data::utxo {
namespace {

OutputKey MakeKey(uint64_t i) {
  OutputKey key;
  // MurmurHash3 64-bit finalizer to mix bits of i
  uint64_t h = i;
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;

  // Copy 8 bytes of entropy to the start of the hash
  std::memcpy(key.hash.data(), &h, sizeof(uint64_t));
  // Zero the rest to be deterministic
  std::memset(key.hash.data() + 8, 0, 32 - 8);
  
  key.index = static_cast<uint16_t>(i);
  return key;
}

TEST(FilterTest, BasicCorrectness) {
  Filter filter;
  constexpr int kNumEntries = 10000;
  
  filter.Reset(kNumEntries);
  
  // Add keys [0, kNumEntries)
  for (int i = 0; i < kNumEntries; ++i) {
    filter.Add(MakeKey(i));
  }

  // Verify all added keys are present
  for (int i = 0; i < kNumEntries; ++i) {
    EXPECT_TRUE(filter.MayContain(MakeKey(i))) << "False negative at index " << i;
  }
}

TEST(FilterTest, FalsePositiveRate) {
  Filter filter;
  constexpr int kNumEntries = 100000;
  constexpr int kNumQueries = 1000000;
  
  filter.Reset(kNumEntries);
  
  // Add keys [0, kNumEntries)
  for (int i = 0; i < kNumEntries; ++i) {
    filter.Add(MakeKey(i));
  }

  // Query keys [kNumEntries, kNumEntries + kNumQueries)
  // These keys were NOT added.
  int false_positives = 0;
  for (int i = kNumEntries; i < kNumEntries + kNumQueries; ++i) {
    if (filter.MayContain(MakeKey(i))) {
      false_positives++;
    }
  }

  double fpr = static_cast<double>(false_positives) / kNumQueries;
  std::cout << "False Positive Rate: " << fpr * 100.0 << "%" << std::endl;

  // Expected FPR for 10 bits/entry with k=6 is approx 0.008 (0.8%) to 0.01 (1.0%).
  // We allow a small margin for variance.
  EXPECT_LT(fpr, 0.015); // Should be < 1.5%
}

TEST(FilterTest, EmptyFilter) {
  Filter filter;
  filter.Reset(0);
  EXPECT_FALSE(filter.MayContain(MakeKey(1)));
  
  filter.Reset(100);
  // No keys added
  EXPECT_FALSE(filter.MayContain(MakeKey(1)));
}

}  // namespace
}  // namespace hornet::data::utxo
