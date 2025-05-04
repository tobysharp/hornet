#include "message_buffer.h"
#include <cstdint>
#include <gtest/gtest.h>

TEST(MessageBufferTest, BasicSerialization) {
  MessageBuffer buffer;

  buffer.Add(uint8_t{0x12});                  // 1 byte
  buffer.Add(uint16_t{0x3456});               // 2 bytes, LE
  buffer.AddBigEndian(uint16_t{0x3456});      // 2 bytes, BE
  buffer.Add(uint32_t{0x789abcde});           // 4 bytes, LE
  buffer.Add(int32_t{-42});                   // 4 bytes, LE
  buffer.Add(uint64_t{0x0123456789abcdef});   // 8 bytes, LE
  buffer.AddVarInt(0xfc);                     // 1 byte
  buffer.AddVarInt(0xfd);                     // 0xfd + 2 bytes
  buffer.AddVarInt(0x12345678);               // 0xfe + 4 bytes
  buffer.AddVarInt(0x123456789abcdef0);       // 0xff + 8 bytes
  buffer.Add(std::string("abc"));             // 0x03 + 3 bytes
  buffer.Add(true);                           // 1 byte

  auto bytes = buffer.AsBytes();
  std::vector<uint8_t> expected = {
    0x12,
    0x56, 0x34,
    0x34, 0x56,
    0xde, 0xbc, 0x9a, 0x78,
    0xd6, 0xff, 0xff, 0xff, // -42 as int32_t
    0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    0xfc,
    0xfd, 0xfd, 0x00,
    0xfe, 0x78, 0x56, 0x34, 0x12,
    0xff, 0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x03, 'a', 'b', 'c',
    0x01
  };

  EXPECT_EQ(bytes.size(), expected.size());
  EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), expected.begin()));
}

