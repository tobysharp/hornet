// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "net/socket.h"

#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

namespace hornet::net {
namespace {

constexpr uint16_t kTestPort = 54321;

// A simple echo server that runs in a background thread.
void RunEchoServer(int listen_fd) {
  int client_fd = accept(listen_fd, nullptr, nullptr);
  char buffer[1024];
  ssize_t bytes = read(client_fd, buffer, sizeof(buffer));
  if (bytes > 0) {
    write(client_fd, buffer, bytes);  // echo back
  }
  close(client_fd);
  close(listen_fd);
}

TEST(SocketTest, ConnectReadWrite) {
  // Setup server socket in main thread to ensure listen() is complete
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listen_fd, 0);

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(kTestPort);

  int opt = 1;
  ASSERT_EQ(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)), 0);
  ASSERT_EQ(bind(listen_fd, (sockaddr*)&addr, sizeof(addr)), 0);
  ASSERT_EQ(listen(listen_fd, 1), 0);

  std::thread server_thread(RunEchoServer, listen_fd);

  Socket sock;
  for (int i = 0; i < 20; ++i) {
    try {
      sock = Socket::Connect("127.0.0.1", kTestPort);
      break;
    } catch (...) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  ASSERT_TRUE(sock.IsOpen());

  std::vector<uint8_t> message = {'B', 'T', 'C'};
  auto written = sock.Write(message);
  ASSERT_TRUE(written.has_value());
  ASSERT_EQ(*written, message.size());

  std::vector<uint8_t> buffer(16);
  auto read = sock.Read(buffer);
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(*read, message.size());
  ASSERT_TRUE(std::equal(buffer.begin(), buffer.begin() + *read, message.begin()));

  sock.Close();
  ASSERT_FALSE(sock.IsOpen());

  server_thread.join();
}

}  // namespace
}  // namespace hornet::net
