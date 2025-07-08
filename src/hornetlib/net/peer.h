// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "hornetlib/net/connection.h"
#include "hornetlib/net/constants.h"
#include "hornetlib/protocol/capabilities.h"
#include "hornetlib/protocol/handshake.h"

namespace hornet::net {

class Peer;
using SharedPeer = std::shared_ptr<Peer>;
using WeakPeer = std::weak_ptr<Peer>;
using PeerId = uint64_t;

class Peer {
 public:
  enum class Direction { Inbound, Outbound };

  Peer(const std::string& host, uint16_t port)
      : id_(0), conn_(host, port),
        direction_(Direction::Outbound),
        address_(host),
        handshake_(protocol::Handshake::Role::Outbound) {}
  // Peer(Connection conn, std::string address)
  //     : conn_(std::move(conn)),
  //       direction_(Direction::Outbound),
  //       address_(std::move(address)),
  //       handshake_(protocol::Handshake::Role::Outbound) {}

  PeerId GetId() const {
    return id_;
  }

  bool IsDropped() const {
    return !conn_.GetSocket().IsOpen();
  }

  const std::string& Address() const {
    return address_;
  }
  Connection& GetConnection() {
    return conn_;
  }
  const Connection& GetConnection() const {
    return conn_;
  }

  Direction GetDirection() const {
    return direction_;
  }
  bool IsInbound() const {
    return direction_ == Direction::Inbound;
  }
  bool IsOutbound() const {
    return direction_ == Direction::Outbound;
  }

  protocol::Handshake& GetHandshake() {
    return handshake_;
  }

  protocol::Capabilities& GetCapabilities() {
    return capabilities_;
  }
  const protocol::Capabilities& GetCapabilities() const {
    return capabilities_;
  }

  void Drop() {
    conn_.Drop();
  }

  friend std::ostream& operator<<(std::ostream& os, const Peer& peer) {
    return os << "{ fd = " << peer.conn_.GetSocket().GetFD() << " }";
  }

 private:
  friend class PeerRegistry;
  
  void SetId(PeerId id) {
    id_ = id;
  }

  PeerId id_;
  Connection conn_;
  Direction direction_;
  std::string address_;
  protocol::Handshake handshake_;
  protocol::Capabilities capabilities_;
};

inline bool operator==(WeakPeer a, WeakPeer b) {
  const auto sa = a.lock();
  const auto sb = b.lock();
  return (sa || sb) && (sa == sb);
}

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