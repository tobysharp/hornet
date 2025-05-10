#include "protocol/parser.h"

#include <array>
#include <string>

#include "crypto/hash.h"  // for DoubleSha256
#include "encoding/writer.h"
#include "protocol/constants.h"
#include "protocol/framer.h"
#include "protocol/message.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

class DummyMessage : public Message {
 public:
  void Deserialize(encoding::Reader &r) override {
    r.ReadByte();
    r.ReadLE4();
  }
  void Serialize(encoding::Writer &w) const override {
    w.WriteByte(0x42);
    w.WriteLE4(0xDEADBEEF);
  }
  std::string GetName() const override {
    return "ping";
  }
};

TEST(ParserTest, ParsesValidMessage) {
  Framer framer(Magic::Main);
  framer.Frame(DummyMessage{});

  Parser parser(Magic::Main);
  auto parsed = parser.Parse(framer.Buffer());

  EXPECT_EQ(parsed.header.command, "ping");
  EXPECT_EQ(parsed.payload.size(), 5);
  EXPECT_EQ(parsed.payload[0], 0x42);
}

TEST(ParserTest, FailsOnWrongMagic) {
  const auto buffer = FrameMessage(Magic::Testnet, DummyMessage{});
  EXPECT_THROW(ParseMessage(Magic::Main, buffer), Parser::Error);
}

TEST(ParserTest, FailsOnShortBuffer) {
  std::array<uint8_t, 10> buf = {};
  Parser parser(Magic::Main);
  EXPECT_THROW(parser.Parse(buf), Parser::Error);
}

TEST(ParserTest, FailsOnChecksumMismatch) {
  encoding::Writer writer;
  writer.WriteLE4(static_cast<uint32_t>(Magic::Main));
  writer.WriteBytes(std::array<uint8_t, 12>{});
  writer.WriteLE4(1);      // payload length
  writer.WriteLE4(0);      // fake checksum
  writer.WriteByte(0x42);  // payload

  Parser parser(Magic::Main);
  EXPECT_THROW(parser.Parse(writer.Buffer()), Parser::Error);
}

}  // namespace
}  // namespace hornet::protocol
