#include "encoding/message_reader.h"

#include <gtest/gtest.h>

namespace {

TEST(MessageReaderTest, ReadLEAndBE) {
    uint8_t data[] = {
        0x34, 0x12,             // LE: 0x1234
        0x12, 0x34,             // BE: 0x1234
        0x78, 0x56, 0x34, 0x12  // LE: 0x12345678
    };

    MessageReader r(data);

    uint16_t le2 = r.ReadLE2();
    EXPECT_EQ(le2, 0x1234);

    uint16_t be2 = r.ReadBE2();
    EXPECT_EQ(be2, 0x1234);

    uint32_t le4 = r.ReadLE4();
    EXPECT_EQ(le4, 0x12345678u);
}

TEST(MessageReaderTest, ReadVarInt) {
    uint8_t data[] = {
        0xFC,                   // raw
        0xFD, 0x34, 0x12,       // 0x1234
        0xFE, 0x78, 0x56, 0x34, 0x12  // 0x12345678
    };
    MessageReader r(data);

    EXPECT_EQ(r.ReadVarInt(), 0xFCu);
    EXPECT_EQ(r.ReadVarInt(), 0x1234u);
    EXPECT_EQ(r.ReadVarInt(), 0x12345678u);
}

TEST(MessageReaderTest, ReadVarString) {
    uint8_t data[] = {
        0x05,                   // length
        'h', 'e', 'l', 'l', 'o'
    };
    MessageReader r(data);
    EXPECT_EQ(r.ReadVarString(), std::string("hello"));
}

TEST(MessageReaderTest, ReadBool) {
    uint8_t data[] = { 0x00, 0x01, 0xFF };
    MessageReader r(data);

    EXPECT_FALSE(r.ReadBool());
    EXPECT_TRUE(r.ReadBool());
    EXPECT_TRUE(r.ReadBool());
}

TEST(MessageReaderTest, SeekAndPos) {
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    MessageReader r(data);

    r.Seek(2);
    EXPECT_EQ(r.GetPos(), 2);
    EXPECT_EQ(r.ReadByte(), 0x03);
}

TEST(MessageReaderTest, ThrowsOnOutOfBounds) {
    uint8_t data[] = { 0x01, 0x02 };
    MessageReader r(data);

    EXPECT_NO_THROW(r.ReadByte());
    EXPECT_NO_THROW(r.ReadByte());
    EXPECT_THROW(r.ReadByte(), std::out_of_range);
    EXPECT_THROW(r.Seek(10), std::out_of_range);
    EXPECT_THROW(r.ReadBytes(1), std::out_of_range);
}

}  // namespace
