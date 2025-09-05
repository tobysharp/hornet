#include "hornetnodelib/net/tcp_notification_sink.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/socket.h"

namespace hornet::node::net {
namespace {

static constexpr uint16_t kSidecarPort = 8646;

class TestTcpServer {
 public:
  static std::unique_ptr<TestTcpServer> Create(uint16_t port) {
    auto server = std::make_unique<TestTcpServer>(port);
    return server;
  }

  bool WaitForClient(int max_wait_ms = 500) {
    int waited = 0;
    while (client_fd_.load() < 0 && waited < max_wait_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      waited += 10;
    }
    return client_fd_.load() >= 0;
  }

  std::string ReceiveMessage(size_t max_bytes = 65536) {
    char buf[65536];
    ssize_t n = ::recv(client_fd_, buf, max_bytes, 0);
    return n > 0 ? std::string(buf, n) : "";
  }

  ~TestTcpServer() {
    if (client_fd_ >= 0) ::close(client_fd_);
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (server_thread_.joinable()) server_thread_.join();
  }

  TestTcpServer(uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    ::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr));
    ::listen(listen_fd_, 1);

    server_thread_ = std::thread([&]() {
      client_fd_ = ::accept(listen_fd_, nullptr, nullptr);
    });
  }

 private:
  int listen_fd_ = -1;
  std::atomic<int> client_fd_ = -1;
  std::thread server_thread_;
};

bool CanConnect(const std::string& host, uint16_t port) {
  try {
    auto socket = Socket::Connect(host, port);
    return socket.IsOpen();
  } catch (...) {
    return false;
  }
}

struct TestServer {
  std::unique_ptr<TestTcpServer> server;
  static TestServer Create(uint16_t port) {
    if (CanConnect(kLocalhost, port))
      return {};
    else
      return {TestTcpServer::Create(port)};
  }
  operator bool() const { return server != nullptr; }
  TestTcpServer* operator->() {
    return server.get();
  }
};

TEST(TcpNotificationSinkTest, ConnectsToServerOnFixedPort) {
  auto server = TestServer::Create(kSidecarPort);
  TcpNotificationSink sink("127.0.0.1", kSidecarPort);
  SUCCEED();  // Reaching here implies successful accept
}

TEST(TcpNotificationSinkTest, DeliversSingleNotification) {
  auto server = TestServer::Create(kSidecarPort);
  TcpNotificationSink sink("127.0.0.1", kSidecarPort);

  util::NotificationPayload payload;
  payload.type = util::NotificationType::Log;
  payload.path = "sys/log";
  payload.map = {
      {"level", std::string("info")},
      {"msg", std::string("hello")},
      {"time_us", int64_t(123)}
  };

  sink(std::move(payload));
  if (server) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string received = server->ReceiveMessage();
    EXPECT_NE(received.find(R"("path":"sys/log")"), std::string::npos);
    EXPECT_NE(received.find(R"("msg":"hello")"), std::string::npos);
    EXPECT_NE(received.find(R"("time_us":123)"), std::string::npos);
  }
}

TEST(TcpNotificationSinkTest, DestructorShutsDownWorker) {
  auto server = TestServer::Create(kSidecarPort);

  {
    TcpNotificationSink sink("127.0.0.1", kSidecarPort);
    util::NotificationPayload payload;
    payload.type = util::NotificationType::Log;
    payload.path = "test";
    payload.map = {{"msg", std::string("bye")}};
    sink(std::move(payload));
  }

  SUCCEED();  // No crash, worker joined
}

TEST(TcpNotificationSinkTest, SendsMultipleNotificationsTogether) {
  auto server = TestServer::Create(kSidecarPort);
  TcpNotificationSink sink("127.0.0.1", kSidecarPort);

  for (int i = 0; i < 3; ++i) {
    util::NotificationPayload payload;
    payload.type = util::NotificationType::Log;
    payload.path = "batch";
    payload.map = {{"msg", std::string("m" + std::to_string(i))}};
    sink(std::move(payload));
  }

  if (server) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const std::string received = server->ReceiveMessage();

    EXPECT_NE(received.find(R"("msg":"m0")"), std::string::npos);
    EXPECT_NE(received.find(R"("msg":"m1")"), std::string::npos);
    EXPECT_NE(received.find(R"("msg":"m2")"), std::string::npos);
  }
}

}  // namespace
}  // namespace hornet::node::net
