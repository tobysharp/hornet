#include "crypto/hash.h"

#include <array>
#include <cstring>
#include <iostream>

#include "crypto/sha256.h"
#include "util/big_uint.h"
#include "util/hex.h"

#include <gtest/gtest.h>

namespace hornet::crypto {
namespace {

TEST(HashTest, Sha256HashOfKnownString) {
  const std::string input = "hello";
  const std::string expected = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
  std::ostringstream oss;
  oss << Sha256(input);
  EXPECT_EQ(oss.str(), expected);
}

TEST(HashTest, DoubleSha256HashOfKnownString) {
  const std::string input = "hello";
  const std::string expected = "9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50";
  std::ostringstream oss;
  oss << DoubleSha256(input);
  EXPECT_EQ(oss.str(), expected);
}

TEST(HashTest, ValidHexDigits) {
  using namespace hornet::util;
  EXPECT_EQ(HexValue<'0'>(), 0);
  EXPECT_EQ(HexValue<'9'>(), 9);
  EXPECT_EQ(HexValue<'a'>(), 10);
  EXPECT_EQ(HexValue<'f'>(), 15);
  EXPECT_EQ(HexValue<'A'>(), 10);
  EXPECT_EQ(HexValue<'F'>(), 15);
}

TEST(HashTest, GenesisMerkleRootHash) {
  using namespace hornet::util;
  constexpr auto bytes = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_h;
  // Check first few bytes (reversed)
  EXPECT_EQ(bytes[0], 0x3b);
  EXPECT_EQ(bytes[1], 0xa3);
  EXPECT_EQ(bytes[2], 0xed);
  EXPECT_EQ(bytes[3], 0xfd);
  EXPECT_EQ(bytes[31], 0x4a);  // Originally first hex byte
}

}  // namespace
}  // namespace hornet::crypto