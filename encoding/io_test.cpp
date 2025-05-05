#include "encoding/io.h"
#include "encoding/varint.h"

#include <gtest/gtest.h>

namespace {

// Test the ability to extend the I/O layer with custom stream types,
// while using the same encoding logic.

struct MyWriter { 
    std::vector<char> data;
};

void Dispatch(io::WriteTag, MyWriter& writer, const char* data, size_t size) {
    writer.data.insert(writer.data.end(), data, size);
}

TEST(IoTest, TestCustomWrite) {
    MyWriter writer;
    const uint32_t v = 42u;
    writer << AsVarInt(v);
    EXPECT_EQ(writer.x, static_cast<char>(v));
}

}  // namespace
