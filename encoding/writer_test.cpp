#include "encoding/writer.h"

#include <gtest/gtest.h>

namespace hornet::encoding {
namespace {

TEST(WriterTest, WriteLE_BE) {
  Writer w;

  w.WriteLE2(0x1234);
  w.WriteBE2(0x1234);
  auto buf = w.Buffer();

  // LE: 0x34, 0x12
  EXPECT_EQ(buf[0], 0x34);
  EXPECT_EQ(buf[1], 0x12);

  // BE: 0x12, 0x34
  EXPECT_EQ(buf[2], 0x12);
  EXPECT_EQ(buf[3], 0x34);
}

TEST(WriterTest, WriteVarInt) {
  Writer w;
  w.WriteVarInt(252u);     // Single byte
  w.WriteVarInt(0x1234u);  // 0xFD marker + 2-byte LE
  auto buf = w.Buffer();

  EXPECT_EQ(buf[0], 0xFC);
  EXPECT_EQ(buf[1], 0xFD);
  EXPECT_EQ(buf[2], 0x34);
  EXPECT_EQ(buf[3], 0x12);
}

TEST(WriterTest, SeekAndOverwrite) {
  Writer w;

  // Write placeholder for 4-byte header
  size_t header_pos = w.WriteLE4(0xAAAAAAAA);

  // Write a payload byte
  w.WriteByte(0x42);

  // Now go back and patch the header
  w.SeekPos(header_pos);
  w.WriteLE4(0x12345678);

  const auto &buf = w.Buffer();
  ASSERT_EQ(buf.size(), 5);

  // Expect overwritten bytes
  EXPECT_EQ(buf[0], 0x78);
  EXPECT_EQ(buf[1], 0x56);
  EXPECT_EQ(buf[2], 0x34);
  EXPECT_EQ(buf[3], 0x12);

  // Expect payload unchanged
  EXPECT_EQ(buf[4], 0x42);
}

}  // namespace
}  // namespace hornet::encoding
