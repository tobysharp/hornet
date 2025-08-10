// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/util/hex.h"

#include <gtest/gtest.h>

namespace hornet {
namespace {

TEST(HexTest, TestBytes) {
  const std::array<uint8_t, 1> arr = "4F"_bytes;
  const std::array<uint8_t, 2> arr2 = "4F E3"_bytes;

  EXPECT_EQ(arr[0], 0x4F);
  EXPECT_EQ(arr2[0], 0x4F);
  EXPECT_EQ(arr2[1], 0xE3);
}

TEST(HexTest, TestStripWhitespace) {
  auto a = "0123456789ABCDEF"_bytes;
  auto b = R"(
    01 23
    45
    67
    89 AB CD EF
  )"_bytes;
  EXPECT_EQ(a, b);
}

}  // namespace
}  // namespace hornet
