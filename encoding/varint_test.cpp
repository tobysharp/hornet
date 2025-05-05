#include "encoding/io.h"
#include "encoding/varint.h"

#include <vector>

#include <gtest/gtest.h>

template <>
void Write(std::vector<char>& os, const char* data, size_t size) {
    os.insert(os.end(), data, data + size);
}

namespace {

TEST(EncodingVarIntTest, TestWriteVarInt) {
    std::vector<char> buffer;
    const uint32_t myint = 42;
    buffer << AsVarInt(myint);
    char expected[] = { 0x2A };
    EXPECT_EQ(memcmp(buffer.data(), expected, sizeof(expected)), 0);
}

TEST(EncodingVarIntTest, TestReadVarInt) {
}

}  // namespace
