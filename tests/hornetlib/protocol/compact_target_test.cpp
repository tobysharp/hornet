// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/compact_target.h"

#include <iostream>

#include <gtest/gtest.h>

#include "hornetlib/protocol/target.h"
#include "hornetlib/util/big_uint.h"

namespace hornet::protocol {
namespace {

TEST(CompactTargetTest, ExpandGenesisBitsMatchesExpectedValue) {
  constexpr CompactTarget bits{0x1d00ffff};
  const Target t = bits.Expand();
  ASSERT_TRUE(t.IsValid());
  constexpr Uint256 expected =
      "00000000ffff0000000000000000000000000000000000000000000000000000"_h256;
  EXPECT_EQ(t.Value(), expected);
}

TEST(CompactTargetTest, CompressRoundTrip) {
  constexpr CompactTarget original{0x1b0404cb};
  const Target expanded = original.Expand();
  ASSERT_TRUE(expanded.IsValid());
  const CompactTarget recompressed = CompactTarget::Compress(expanded);
  EXPECT_EQ(recompressed, original);
}

TEST(CompactTargetTest, ExpandReturnsInvalidForZeroMantissa) {
  const CompactTarget bits{0x20000000}; // exponent 0x20, mantissa 0
  const Target t = bits.Expand();
  EXPECT_FALSE(t.IsValid());
}

TEST(CompactTargetTest, ExpandReturnsInvalidForNegativeMantissa) {
  const CompactTarget bits{0x20800001}; // sign bit set and mantissa > 0
  const Target t = bits.Expand();
  EXPECT_FALSE(t.IsValid());
}

TEST(CompactTargetTest, ExpandReturnsInvalidOnOverflow) {
  const CompactTarget bits{0x23010001}; // exponent 0x23 (35) > 34
  const Target t = bits.Expand();
  EXPECT_FALSE(t.IsValid());
}

TEST(CompactTargetTest, SerializeAndDeserializeRoundTrip) {
  const CompactTarget bits{0x1d00ffff};
  encoding::Writer w;
  bits.Serialize(w);
  encoding::Reader r(w.Buffer());
  CompactTarget read_bits;
  read_bits.Deserialize(r);
  EXPECT_EQ(read_bits, bits);
}

TEST(CompactTargetTest, MaximumReturnsConstant) {
  EXPECT_EQ(CompactTarget::Maximum(), CompactTarget{kMaxCompactTarget});
}

}  // namespace hornet::protocol
}  // namespace
