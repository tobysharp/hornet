#pragma once

#include "message/registry.h"
#include "net/socket.h"
#include "protocol/constants.h"
#include "protocol/factory.h"
#include "protocol/message.h"
#include "protocol/parser.h"

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace hornet::net {

class Connection {
 public:
  Connection(Socket sock, protocol::Magic magic)
      : sock_(std::move(sock)), parser_(magic) {}

  std::unique_ptr<protocol::Message> NextMessage(
      int timeout_ms = -1);

  bool IsPartial() const;
  bool IsWaiting() const;

 private:
  std::span<const uint8_t> GetUnparsedData() const;
  bool IsCompleteMessageBuffered() const;
  std::unique_ptr<protocol::Message> TryParseMessage();

  Socket sock_;
  const protocol::Factory factory_ = message::CreateMessageFactory();
  const protocol::Parser parser_;
  std::vector<uint8_t> buffer_;
  size_t write_at_ = 0;
  size_t parse_at_ = 0;

  static constexpr size_t kReadChunkSize = 1 << 11;  // 2 KiB
  static constexpr size_t kTrimThreshold = 1 << 20;  // 1 MiB
  static constexpr size_t kReadLimit = 4 << 20;      // 4 MiB
};

}  // namespace hornet::net
