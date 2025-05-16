#include "protocol/framer.h"

#include "encoding/reader.h"
#include "encoding/writer.h"
#include "protocol/constants.h"
#include "protocol/message.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

class DummyMessage : public Message {
 public:
  std::string GetName() const override {
    return "dummy";
  }
  void Serialize(encoding::Writer &w) const override {
    w.WriteLE4(0xDEADBEEF);
  }
  void Deserialize(encoding::Reader &r) override {
    r.ReadLE4();
  }
  void Accept(message::Visitor& visitor) const override {}
};

TEST(MessageFramerTest, FrameFormatsCorrectHeader) {
  DummyMessage m;
  Framer framer(Magic::Main);

  framer.Frame(m);
  const auto &buf = framer.Buffer();

  ASSERT_GE(buf.size(), 24 + 4);  // header + 4-byte payload

  // Magic bytes
  EXPECT_EQ(buf[0], 0xF9);
  EXPECT_EQ(buf[1], 0xBE);
  EXPECT_EQ(buf[2], 0xB4);
  EXPECT_EQ(buf[3], 0xD9);

  // Command (null padded)
  std::string command_field(reinterpret_cast<const char *>(&buf[4]), 12);
  EXPECT_EQ(command_field.substr(0, 5), "dummy");
  EXPECT_EQ(command_field.substr(5), std::string(7, '\0'));

  // Payload length
  uint32_t len = *reinterpret_cast<const uint32_t *>(&buf[16]);
  EXPECT_EQ(len, 4u);

  // Payload content
  uint32_t payload = *reinterpret_cast<const uint32_t *>(&buf[24]);
  EXPECT_EQ(payload, 0xDEADBEEF);
}

}  // namespace
}  // namespace hornet::protocol
