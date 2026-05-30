// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "hornetlib/crypto/curve.h"
#include "hornetlib/crypto/reduce.h"
#include "hornetlib/util/hex.h"

namespace hornet::crypto::ecdsa {
namespace {

Uint256 ReferenceReduceModuloP(const Uint256& x, const Uint256& y) {
  return x.MultiplyWide(y).Modulo(constants::p);
}

uint64_t XorShift64(uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

Uint256 RandomUint256(uint64_t& state) {
  return Uint256{std::array<uint64_t, 4>{XorShift64(state), XorShift64(state), XorShift64(state), XorShift64(state)}};
}

TEST(ReduceModuloPTest, MatchesReferenceForEdgeCases) {
  const std::array<Uint256, 11> values = {
      Uint256::Zero(),
      Uint256{1},
      Uint256{2},
      Uint256{977},
      Uint256{uint64_t{1} << 32},
      constants::p - Uint256{2},
      constants::p - Uint256{1},
      constants::p,
      constants::p + Uint256{1},
      "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"_h256,
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"_h256,
  };

  for (const auto& x : values) {
    for (const auto& y : values) {
      EXPECT_EQ(ReduceModuloP(x, y), ReferenceReduceModuloP(x, y)) << "x=" << x << ", y=" << y;
    }
  }
}

TEST(ReduceModuloPTest, MatchesReferenceForValuesAroundModulusBoundary) {
  const std::array<Uint256, 7> near_modulus = {
      constants::p - Uint256{3},
      constants::p - Uint256{2},
      constants::p - Uint256{1},
      constants::p,
      constants::p + Uint256{1},
      constants::p + Uint256{2},
      constants::p + Uint256{3},
  };

  for (const auto& x : near_modulus) {
    for (const auto& y : near_modulus) {
      EXPECT_EQ(ReduceModuloP(x, y), ReferenceReduceModuloP(x, y)) << "x=" << x << ", y=" << y;
    }
  }
}

TEST(ReduceModuloPTest, MatchesReferenceForRandomizedInputs) {
  uint64_t state = 0x9e3779b97f4a7c15ull;

  for (int i = 0; i < 1000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    EXPECT_EQ(ReduceModuloP(x, y), ReferenceReduceModuloP(x, y)) << "iteration=" << i;
  }
}

TEST(ReduceModuloPTest, ResultIsAlwaysCanonicalForRandomizedInputs) {
  uint64_t state = 0xd1b54a32d192ed03ull;

  for (int i = 0; i < 1000; ++i) {
    const Uint256 x = RandomUint256(state);
    const Uint256 y = RandomUint256(state);
    const Uint256 reduced = ReduceModuloP(x, y);
    EXPECT_LT(reduced, constants::p) << "iteration=" << i;
  }
}

}  // namespace
}  // namespace hornet::crypto::ecdsa