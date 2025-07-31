// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/dispatch.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetnodelib/net/socket.h"

namespace hornet::node::net {

constexpr uint32_t kDefaultBufferSize = 2048;  // bytes

template <typename T>
std::unique_ptr<T> ReceiveMessage(const Socket& sock, protocol::Magic magic) {
  std::array<uint8_t, kDefaultBufferSize> buf;
  const auto length = sock.Read(buf);
  if (!length) return nullptr;

  if (*length < 24) throw std::runtime_error("Incomplete message received.");

  if (*reinterpret_cast<const protocol::Magic*>(&buf[0]) != magic)
    throw std::runtime_error("Received incorrect magic bytes.");

  const auto& factory = protocol::MessageFactory::Default();
  return protocol::ParseMessage<T>(factory, magic, {buf.data(), static_cast<size_t>(*length)});
}

}  // namespace hornet::node::net
