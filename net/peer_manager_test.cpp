#include "net/peer_manager.h"

#include <memory>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>

#include "net/peer.h"

#include <gtest/gtest.h>

namespace hornet::net {
namespace {

constexpr uint16_t kTestPort = 54323;

void RunTestServer(int listen_fd) {
  int client_fd = accept(listen_fd, nullptr, nullptr);
  if (client_fd >= 0) {
    char buffer[1];
    read(client_fd, buffer, sizeof(buffer));
    close(client_fd);
  }
  close(listen_fd);
}

TEST(PeerManagerTest, AddAndRemovePeer) {
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

  std::thread server_thread(RunTestServer, listen_fd);

  PeerManager manager;
  manager.AddPeer("127.0.0.1", kTestPort);
  auto write_ready = manager.PollWrite(1000);
  ASSERT_FALSE(write_ready.empty());

  auto peer = *(write_ready.begin());
  EXPECT_EQ(peer->Address(), "127.0.0.1");
  peer->GetConnection().Write(std::array<uint8_t, 1>{'x'});

  manager.RemovePeer(peer);

  server_thread.join();
}

}  // namespace
}  // namespace hornet::net
