// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/net/peer.h"

#include <netinet/in.h>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>

namespace hornet::net {
namespace {

constexpr uint16_t kTestPort = 54324;

void RunEchoServer(int listen_fd) {
  int client_fd = accept(listen_fd, nullptr, nullptr);
  if (client_fd >= 0) {
    char buffer[1024];
    ssize_t bytes = read(client_fd, buffer, sizeof(buffer));
    if (bytes > 0) {
      write(client_fd, buffer, bytes);
    }
    close(client_fd);
  }
  close(listen_fd);
}

TEST(PeerTest, SocketEchoThroughPeerConnection) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listen_fd, 0);

  int opt = 1;
  ASSERT_EQ(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)), 0);

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(kTestPort);

  ASSERT_EQ(bind(listen_fd, (sockaddr*)&addr, sizeof(addr)), 0);
  ASSERT_EQ(listen(listen_fd, 1), 0);

  std::thread server_thread(RunEchoServer, listen_fd);

  Peer peer("127.0.0.1", kTestPort);
  std::vector<uint8_t> msg = {'B', 'T', 'C'};
  size_t written = peer.GetConnection().Write(msg);
  ASSERT_EQ(written, msg.size());

  size_t read = 0;
  for (int i = 0; i < 100 && read == 0; ++i) {
    read = peer.GetConnection().ReadToBuffer(16);
    if (read == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto echoed = peer.GetConnection().PeekBufferedData();
  ASSERT_EQ(echoed.size(), msg.size());
  ASSERT_TRUE(std::equal(echoed.begin(), echoed.end(), msg.begin()));

  peer.GetConnection().ConsumeBufferedData(read);
  ASSERT_TRUE(peer.GetConnection().PeekBufferedData().empty());

  server_thread.join();
}

}  // namespace
}  // namespace hornet::net
