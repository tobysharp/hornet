#include "encoding/io.h"
#include "encoding/io_stream.h"
#include "encoding/memory_stream.h"
#include "encoding/varint.h"

#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(EncodingVarIntTest, TestWriteVarInt) {
    using namespace encoding;

    MemoryOStream stream;
    const uint32_t myint = 42;
    // std::ostream& operator <<(std::ostream&, Wrapper<Encoding::kVarInt, const uint32_t>);
    // or
    // MemoryOStream& operator <<(MemoryOStream&, Wrapper<Encoding::kVarInt, const uint32_t>);
    // stream << AsVarInt(myint);

    //operator << <typename encoding::VarInt, uint32_t, std::ostream>(stream, As<encoding::VarInt, const uint32_t>(myint)); 
    stream << As<VarInt>(myint);
    // std::ostream& operator <<(std::ostream&, uint32_t);
    // ^ This would be dangerous as it's overriding general behavior
    // or
    // MemoryOStream& operator <<(MemoryOStream&, uint32_t);
    // ^ Can forward to std::ostream& operator <<(std::ostream&, Wrapper<Encoding::kDefault, uint32_t>);
    stream << myint;            
    uint8_t expected[] = { 0x2A };
    const uint8_t* compare = stream.Buffer().data();
    EXPECT_EQ(memcmp(compare, &expected, sizeof(expected)), 0);
    //EXPECT_EQ(*reinterpret_cast<const uint32_t*>(compare + 1), myint);
}

TEST(EncodingVarIntTest, TestReadVarInt) {
    const std::vector<uint8_t> buffer = { 0x2A };
    MemoryIStream stream(buffer);
    uint32_t myint = 0;
    stream >> AsVarInt(myint);
    EXPECT_EQ(myint, 42);
}

}  // namespace
