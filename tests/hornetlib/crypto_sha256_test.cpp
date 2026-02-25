// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <gtest/gtest.h>

#include "hornetlib/crypto/cpuinfo.h"
#include "hornetlib/crypto/hash.h"
#include "hornetlib/crypto/sha256.h"
#include "hornetlib/crypto/sha256_ni.h"

using namespace hornet::crypto;

// Test that SHA-NI produces same results as scalar implementation
TEST(SHA256_SHANI, MatchesScalarImplementation) {
  if (!HasSHAExtensions()) {
    GTEST_SKIP() << "SHA-NI not supported on this CPU";
  }

  // Test various input sizes
  std::vector<size_t> test_sizes = {0, 1, 32, 55, 64, 80, 100, 127, 128, 1000};

  for (size_t size : test_sizes) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>(i * 7 + 13);
    }

    auto scalar_hash = SHA256::Hash(data);
    auto shani_hash = SHA256::Hash_SHANI(data);

    EXPECT_EQ(scalar_hash, shani_hash) << "Mismatch at size " << size;
  }
}

// Test known SHA256 test vectors
TEST(SHA256_SHANI, KnownTestVectors) {
  if (!HasSHAExtensions()) {
    GTEST_SKIP() << "SHA-NI not supported on this CPU";
  }

  // Test vector: empty string
  {
    std::vector<uint8_t> data;
    auto hash = SHA256::Hash_SHANI(data);
    // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    bytes32_t expected = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    EXPECT_EQ(hash, expected);
  }

  // Test vector: "abc"
  {
    std::vector<uint8_t> data = {'a', 'b', 'c'};
    auto hash = SHA256::Hash_SHANI(data);
    // SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    bytes32_t expected = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    EXPECT_EQ(hash, expected);
  }
}
