#include "hornetlib/protocol/script/writer.h"

#include <numeric>

#include "hornetlib/protocol/script/view.h"

#include <gtest/gtest.h>

namespace hornet::protocol::script {
namespace {

TEST(ScriptWriterTest, PushZero) {
  Writer w;
  w.PushInt(0);
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 1);
  EXPECT_EQ(bytes[0], static_cast<uint8_t>(Op::PushConst0));
}

TEST(ScriptWriterTest, PushSmallPositiveImmediate) {
  for (int i = 1; i <= 16; ++i) {
    Writer w;
    w.PushInt(i);
    auto bytes = std::span<const uint8_t>(w);
    ASSERT_EQ(bytes.size(), 1);
    EXPECT_EQ(bytes[0], static_cast<uint8_t>(Op::PushConst1 + (i - 1)));
  }
}

TEST(ScriptWriterTest, PushNegativeOneImmediate) {
  Writer w;
  w.PushInt(-1);
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 1);
  EXPECT_EQ(bytes[0], static_cast<uint8_t>(Op::PushConst1 + (-2)));  // i.e., Push(-1)
}

TEST(ScriptWriterTest, PushLargePositiveNoHighBit) {
  Writer w;
  w.PushInt(1000);  // 0x03E8 → bytes [0xE8, 0x03]
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 3);
  EXPECT_EQ(bytes[0], 2);         // size prefix
  EXPECT_EQ(bytes[1], 0xE8);      // little-endian
  EXPECT_EQ(bytes[2], 0x03);
}

TEST(ScriptWriterTest, PushLargePositiveHighBitSet) {
  Writer w;
  w.PushInt(0x80);  // 128 decimal, high bit set → needs extra 0x00 byte
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 3);
  EXPECT_EQ(bytes[0], 2);         // size prefix
  EXPECT_EQ(bytes[1], 0x80);      // value
  EXPECT_EQ(bytes[2], 0x00);      // extra byte added
}

TEST(ScriptWriterTest, PushNegativeMultiByte) {
  Writer w;
  w.PushInt(-1000);
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 3);
  EXPECT_EQ(bytes[0], 2);         // size prefix
  EXPECT_EQ(bytes[1], 0xE8);      // abs(1000) = 0x03E8
  EXPECT_EQ(bytes[2], 0x83);      // 0x03 | 0x80 (sign bit set)
}

TEST(ScriptWriterTest, PushThreeBytePositive) {
  Writer w;
  w.PushInt(0x123456);  // 0x56 0x34 0x12
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 4);
  EXPECT_EQ(bytes[0], 3);         // size prefix
  EXPECT_EQ(bytes[1], 0x56);
  EXPECT_EQ(bytes[2], 0x34);
  EXPECT_EQ(bytes[3], 0x12);
}

TEST(ScriptWriterTest, PushThreeByteNegative) {
  Writer w;
  w.PushInt(-0x123456);  // 0x56 0x34 0x92 (0x12 | 0x80)
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 4);
  EXPECT_EQ(bytes[0], 3);         // size prefix
  EXPECT_EQ(bytes[1], 0x56);
  EXPECT_EQ(bytes[2], 0x34);
  EXPECT_EQ(bytes[3], 0x92);      // high bit set (0x12 | 0x80)
}

TEST(ScriptWriterTest, PushFourBytePositive) {
  Writer w;
  w.PushInt(0x12345678);  // 0x78 0x56 0x34 0x12
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 5);
  EXPECT_EQ(bytes[0], 4);         // size prefix
  EXPECT_EQ(bytes[1], 0x78);
  EXPECT_EQ(bytes[2], 0x56);
  EXPECT_EQ(bytes[3], 0x34);
  EXPECT_EQ(bytes[4], 0x12);
}

TEST(ScriptWriterTest, PushFourByteNegative) {
  Writer w;
  w.PushInt(-0x12345678);  // 0x78 0x56 0x34 0x92 (0x12 | 0x80)
  auto bytes = std::span<const uint8_t>(w);
  ASSERT_EQ(bytes.size(), 5);
  EXPECT_EQ(bytes[0], 4);         // size prefix
  EXPECT_EQ(bytes[1], 0x78);
  EXPECT_EQ(bytes[2], 0x56);
  EXPECT_EQ(bytes[3], 0x34);
  EXPECT_EQ(bytes[4], 0x92);      // high bit set (0x12 | 0x80)
}

}  // namespace
}  // namespace hornet::protocol::script
