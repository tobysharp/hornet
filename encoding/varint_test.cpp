#include "encoding/io.h"
#include "encoding/io_stream.h"
#include "encoding/memory_stream.h"
#include "encoding/varint.h"

#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(EncodingVarIntTest, TestWriteVarInt) {
    MemoryOStream stream;
    const uint32_t myint = 42;
    stream << AsVarInt(myint);
    uint8_t expected[] = { 0x2A };
    EXPECT_EQ(memcmp(stream.Buffer().data(), expected, sizeof(expected)), 0);
}

TEST(EncodingVarIntTest, TestReadVarInt) {
    const std::vector<uint8_t> buffer = { 0x2A };
    MemoryIStream stream(buffer);
    uint32_t myint = 0;
    stream >> AsVarInt(myint);
    EXPECT_EQ(myint, 42);
}

}  // namespace
