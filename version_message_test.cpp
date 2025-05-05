#include "version_message.h"
#include "message_buffer.h"
#include "message_builder.h"
#include "types.h"

#include <gtest/gtest.h>
#include <sstream>
#include <iomanip>

TEST(VersionMessageTest, SerializesCorrectly) {
  VersionMessage msg;
  msg.timestamp = 1700000000;

  MessageBuffer buffer;
  msg.Serialize(buffer);

  // Expected: payload size is nonzero
  auto bytes = buffer.AsBytes();
  ASSERT_GT(bytes.size(), 0);

  // Optional: print hex
  std::ostringstream hex;
  for (uint8_t b : bytes)
    hex << std::hex << std::setw(2) << std::setfill('0') << int{b};

  EXPECT_FALSE(hex.str().empty());
}

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

TEST(VersionMessageTest, SendVersionMessage) {
  // Set up socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_NE(sock, -1);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8333); // mainnet port
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  ASSERT_NE(connect(sock, (sockaddr*)&addr, sizeof(addr)), -1);

  // Create and serialize the version message
  VersionMessage msg;
  msg.timestamp = 1700000000;
  msg.user_agent = "/btchornet:test/";

  MessageBuilder builder;
  builder << "version" << msg;

  auto bytes = builder.AsBytes();
  send(sock, bytes.data(), bytes.size(), 0);

  // Receive response
  uint8_t header[24];
  size_t n = recv(sock, header, sizeof(header), 0);
  ASSERT_EQ(n, 24) << "Expected 24-byte message header from bitcoind";

  // Expect response command to be "versio"
  std::string command(reinterpret_cast<char*>(header + 4), 12);
  EXPECT_EQ(command.substr(0, 6), "versio");

  close(sock);
}