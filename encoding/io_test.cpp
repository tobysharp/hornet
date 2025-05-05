#include "encoding/io.h"
#include "encoding/varint.h"

#include <algorithm>

#include <gtest/gtest.h>

namespace {

// Test the ability to extend the I/O layer with custom stream types,
// while using the same encoding logic.

struct MyWriter { 
    std::vector<char> data;
};

void Dispatch(io::WriteTag, MyWriter& writer, const char* data, size_t size) {
    writer.data.insert(writer.data.end(), data, data + size);
}

TEST(IoTest, TestCustomWrite) {
    MyWriter writer;
    const uint32_t v = 42u;
    writer << AsVarInt(v);
    EXPECT_EQ(writer.data.size(), 1);
    EXPECT_EQ(writer.data[0], static_cast<char>(v));
}

struct MyReader {
    std::span<const char> span;
};

void Dispatch(io::ReadTag, MyReader& reader, char* data, size_t size) {
    std::copy(reader.span.begin(), reader.span.end(), data);
}

TEST(IoTest, TestCustomRead) {
    const std::vector<char> vec = { 0x2A };
    MyReader reader = { vec };
    uint32_t v = 0;
    reader >> AsVarInt(v);
    EXPECT_EQ(v, 42u);
}

}  // namespace
