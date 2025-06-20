// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "net/connection.h"
#include "net/constants.h"
#include "protocol/capabilities.h"
#include "protocol/handshake.h"

namespace hornet::net {

class Peer;
using PeerId = std::weak_ptr<Peer>;

class Peer {
  public:
  Peer(const std::string& host, uint16_t port)
      : conn_(host, port), address_(host), handshake_(protocol::Handshake::Role::Outbound) {}
  Peer(Connection conn, std::string address)
      : conn_(std::move(conn)), address_(std::move(address)), handshake_(protocol::Handshake::Role::Outbound) {}

  static std::shared_ptr<Peer> FromId(PeerId id) {
    // If in the future we change PeerId to an integer type for persistence, then we can replace this
    // call to std::weak_ptr::lock with one like PeerManager::Instance().Lookup(id) etc.
    return id.lock();
  }

  static bool IsSame(PeerId a, PeerId b) {
    return FromId(a) == FromId(b);
  }

  bool IsDropped() const {
    return !conn_.GetSocket().IsOpen();
  }

  const std::string& Address() const { return address_; }
  Connection& GetConnection() { return conn_; }
  const Connection& GetConnection() const { return conn_; }

  protocol::Handshake& GetHandshake() { return handshake_; }

  protocol::Capabilities& GetCapabilities() { return capabilities_; }
  const protocol::Capabilities& GetCapabilities() const { return capabilities_; }

  void Drop() {
    conn_.Drop();
  }

  friend std::ostream& operator <<(std::ostream& os, const Peer& peer) {
    return os << "{ fd = " << peer.conn_.GetSocket().GetFD() << " }";
  }
 private:
  Connection conn_;
  std::string address_;
  protocol::Handshake handshake_;
  protocol::Capabilities capabilities_;
};

}  // namespace hornet::net

/*
struct SendJob {
  std::shared_ptr<const std::vector<uint8_t>> buffer;
  size_t cursor = 0;
};

struct PeerContext {
  Connection conn;
  protocol::Magic magic;
  protocol::HandshakeState handshake = protocol::HandshakeState::Disconnected;

  std::vector<uint8_t> recv_buffer;
  size_t parse_cursor = 0;

  std::deque<std::unique_ptr<protocol::Message>> inbound_queue;
  std::deque<SendJob> outbound_queue;

  protocol::MessageFactory factory = protocol::CreateMessageFactory();
};
*/