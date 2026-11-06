// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/encoding/reader.h"

#include <gtest/gtest.h>

namespace hornet::encoding {
namespace {

TEST(ReaderTest, ReadLEAndBE) {
  uint8_t data[] = {
      0x34, 0x12,             // LE: 0x1234
      0x12, 0x34,             // BE: 0x1234
      0x78, 0x56, 0x34, 0x12  // LE: 0x12345678
  };

  Reader r(data);

  uint16_t le2 = r.ReadLE2();
  EXPECT_EQ(le2, 0x1234);

  uint16_t be2 = r.ReadBE2();
  EXPECT_EQ(be2, 0x1234);

  uint32_t le4 = r.ReadLE4();
  EXPECT_EQ(le4, 0x12345678u);
}

TEST(MessageReaderTest, ReadVarInt) {
  uint8_t data[] = {
      0xFC,                         // raw
      0xFD, 0x34, 0x12,             // 0x1234
      0xFE, 0x78, 0x56, 0x34, 0x12  // 0x12345678
  };
  Reader r(data);

  EXPECT_EQ(r.ReadVarInt(), 0xFCu);
  EXPECT_EQ(r.ReadVarInt(), 0x1234u);
  EXPECT_THROW(r.ReadVarInt(), std::out_of_range);
}

// A CompactSize MUST use the shortest encoding for its value, as Core enforces with "non-canonical ReadCompactSize()".
TEST(MessageReaderTest, ReadVarIntRejectsNonCanonicalEncodings) {
  const uint8_t two_byte_small[] = {0xFD, 0xFC, 0x00};                                        // 252 needs one byte.
  const uint8_t four_byte_small[] = {0xFE, 0xFF, 0xFF, 0x00, 0x00};                           // 65535 needs two.
  const uint8_t eight_byte_small[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};  // 2^32-1 needs four.

  EXPECT_THROW(Reader(two_byte_small).ReadVarInt(), std::out_of_range);
  EXPECT_THROW(Reader(four_byte_small).ReadVarInt(), std::out_of_range);
  EXPECT_THROW(Reader(eight_byte_small).ReadVarInt(false), std::out_of_range);
}

TEST(MessageReaderTest, ReadVarIntAcceptsMinimalBoundaries) {
  const uint8_t two_byte_min[] = {0xFD, 0xFD, 0x00};                                        // 253
  const uint8_t four_byte_min[] = {0xFE, 0x00, 0x00, 0x01, 0x00};                           // 65536
  const uint8_t eight_byte_min[] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};  // 2^32

  EXPECT_EQ(Reader(two_byte_min).ReadVarInt(), 253u);
  EXPECT_EQ(Reader(four_byte_min).ReadVarInt(), 65536u);
  EXPECT_EQ(Reader(eight_byte_min).ReadVarInt(false), 1ull << 32);
}

TEST(ReaderTest, ReadVarString) {
  uint8_t data[] = {0x05,  // length
                    'h',  'e', 'l', 'l', 'o'};
  Reader r(data);
  EXPECT_EQ(r.ReadVarString(), std::string("hello"));
}

TEST(ReaderTest, ReadBool) {
  uint8_t data[] = {0x00, 0x01, 0xFF};
  Reader r(data);

  EXPECT_FALSE(r.ReadBool());
  EXPECT_TRUE(r.ReadBool());
  EXPECT_TRUE(r.ReadBool());
}

TEST(ReaderTest, SeekAndPos) {
  uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  Reader r(data);

  r.Seek(2);
  EXPECT_EQ(r.GetPos(), 2);
  EXPECT_EQ(r.ReadByte(), 0x03);
}

TEST(ReaderTest, ThrowsOnOutOfBounds) {
  uint8_t data[] = {0x01, 0x02};
  Reader r(data);

  EXPECT_NO_THROW(r.ReadByte());
  EXPECT_NO_THROW(r.ReadByte());
  EXPECT_THROW(r.ReadByte(), std::out_of_range);
  EXPECT_THROW(r.Seek(10), std::out_of_range);
  EXPECT_THROW(r.ReadBytes(1), std::out_of_range);
}

}  // namespace
}  // namespace hornet::encoding
