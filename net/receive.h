#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

#include "net/socket.h"
#include "message/registry.h"
#include "protocol/constants.h"
#include "protocol/dispatch.h"
#include "protocol/factory.h"

namespace hornet::net {

constexpr uint32_t kDefaultBufferSize = 2048;  // bytes

template <typename T>
std::unique_ptr<T> ReceiveMessage(const Socket& sock, protocol::Magic magic) {
  std::array<uint8_t, kDefaultBufferSize> buf;
  const size_t length = sock.Read(buf);

  if (length < 24) throw std::runtime_error("Incomplete message received.");

  if (*reinterpret_cast<const protocol::Magic*>(&buf[0]) != magic)
    throw std::runtime_error("Received incorrect magic bytes.");

  const auto factory = message::CreateMessageFactory();
  return protocol::ParseMessage<T>(factory, magic, {buf.data(), length});
}

}  // namespace hornet::net
