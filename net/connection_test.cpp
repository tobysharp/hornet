#include "net/connection.h"

#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "util/shared_span.h"

#include <gtest/gtest.h>

namespace hornet::net {
namespace {

constexpr uint16_t kTestPort = 54322;

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

TEST(ConnectionTest, WriteAndReadEcho) {
  // Setup server socket in main thread
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

  Connection conn("127.0.0.1", kTestPort);

  std::vector<uint8_t> message = {'B', 'T', 'C'};
  size_t written = conn.Write(message);
  ASSERT_EQ(written, message.size());

  // Retry ReadToBuffer up to 1 second if needed
  size_t read = 0;
  for (int i = 0; i < 100 && read == 0; ++i) {
    read = conn.ReadToBuffer(16);
    if (read == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_EQ(read, message.size());

  auto data = conn.PeekBufferedData();
  ASSERT_EQ(data.size(), message.size());
  ASSERT_TRUE(std::equal(data.begin(), data.end(), message.begin()));

  conn.ConsumeBufferedData(read);
  ASSERT_TRUE(conn.PeekBufferedData().empty());

  server_thread.join();
}

TEST(ConnectionTest, EnqueueWriteAndFlush) {
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

  Connection conn("127.0.0.1", kTestPort);

  auto vec = std::make_shared<std::vector<uint8_t>>(3, 0x42);  // {'B', 'B', 'B'}
  conn.EnqueueWrite(util::SharedSpan<const uint8_t>(vec));

  size_t written = conn.ContinueWrite();
  ASSERT_EQ(written, 3);
  ASSERT_EQ(conn.QueuedWriteBufferCount(), 0);  // queue should be empty

  server_thread.join();
}

TEST(ConnectionTest, EnqueueTwoBuffersAndFlushInSteps) {
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

  Connection conn("127.0.0.1", kTestPort);

  auto buf1 = std::make_shared<std::vector<uint8_t>>(3, 'X');
  auto buf2 = std::make_shared<std::vector<uint8_t>>(3, 'Y');

  conn.EnqueueWrite(util::SharedSpan<const uint8_t>(*buf1, buf1));
  conn.EnqueueWrite(util::SharedSpan<const uint8_t>(*buf2, buf2));

  // First call should flush the first buffer
  size_t first = conn.ContinueWrite();
  ASSERT_EQ(first, 3);
  ASSERT_GT(conn.QueuedWriteBufferCount(), 0);

  // Second call should flush the second buffer
  size_t second = conn.ContinueWrite();
  ASSERT_EQ(second, 3);
  ASSERT_EQ(conn.QueuedWriteBufferCount(), 0);  // queue should be empty

  server_thread.join();
}

}  // namespace
}  // namespace hornet::net
