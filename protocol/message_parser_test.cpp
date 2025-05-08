#include "encoding/message_writer.h"
#include "hash.h"  // for DoubleSha256
#include "protocol/constants.h"
#include "protocol/message.h"
#include "protocol/message_framer.h"
#include "protocol/message_parser.h"

#include <array>
#include <string>

#include <gtest/gtest.h>

namespace {

class DummyMessage : public Message {
    public:
        void Deserialize(MessageReader& r) override {
            r.ReadByte();
            r.ReadLE4();
        }
        void Serialize(MessageWriter& w) const override {
            w.WriteByte(0x42);
            w.WriteLE4(0xDEADBEEF);
        }
        std::string GetName() const override {
            return "ping";
        }
};

TEST(MessageParserTest, ParsesValidMessage) {
    MessageFramer framer(Magic::Main);
    framer.Frame(DummyMessage{});

    MessageParser parser(Magic::Main);
    auto parsed = parser.Parse(framer.Buffer());

    EXPECT_EQ(parsed.command, "ping");
    EXPECT_EQ(parsed.payload.size(), 5);
    EXPECT_EQ(parsed.payload[0], 0x42);
}

TEST(MessageParserTest, FailsOnWrongMagic) {
    const auto buffer = FrameMessage(Magic::Testnet, DummyMessage{});
    EXPECT_THROW(ParseMessage(Magic::Main, buffer), MessageParser::Error);
}

TEST(MessageParserTest, FailsOnShortBuffer) {
    std::array<uint8_t, 10> buf = {};
    MessageParser parser(Magic::Main);
    EXPECT_THROW(parser.Parse(buf), MessageParser::Error);
}

TEST(MessageParserTest, FailsOnChecksumMismatch) {
    MessageWriter writer;
    writer.WriteLE4(static_cast<uint32_t>(Magic::Main));
    writer.WriteBytes(std::array<uint8_t, 12>{});
    writer.WriteLE4(1); // payload length
    writer.WriteLE4(0); // fake checksum
    writer.WriteByte(0x42); // payload

    MessageParser parser(Magic::Main);
    EXPECT_THROW(parser.Parse(writer.Buffer()), MessageParser::Error);
}

}  // namespace
