// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <atomic>
#include <memory>
#include <ostream>
#include <string>

#include "hornetlib/protocol/capabilities.h"
#include "hornetlib/protocol/handshake.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/thread_safe_queue.h"
#include "hornetnodelib/net/connection.h"
#include "hornetnodelib/net/constants.h"
#include "hornetnodelib/net/serialization_memo.h"

namespace hornet::node::net {

class Peer;
using SharedPeer = std::shared_ptr<Peer>;
using WeakPeer = std::weak_ptr<Peer>;
using PeerId = uint64_t;

class Peer {
 public:
  using SharedOutboundMessage = std::shared_ptr<SerializationMemo>;
  using OutQueue = util::ThreadSafeQueue<SharedOutboundMessage>;

  enum class Direction { Inbound, Outbound };

  Peer(const std::string& host, uint16_t port)
      : id_(0), conn_(host, port),
        direction_(Direction::Outbound),
        address_(host),
        handshake_(protocol::Handshake::Role::Outbound) {}

  PeerId GetId() const {
    return id_;
  }

  bool IsDropped() const {
    return dropped_.test() || !conn_.GetSocket().IsOpen();
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
    if (!dropped_.test_and_set())
      LogWarn() << "Dropping peer " << id_ << ".";
  }

  friend std::ostream& operator<<(std::ostream& os, const Peer& peer) {
    return os << "{ id = " << peer.id_ << " }";
  }

  OutQueue& Outbox() { return outbox_; }
  const OutQueue& Outbox() const { return outbox_; }

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
  OutQueue outbox_;
  std::atomic_flag dropped_;
};

inline bool operator==(WeakPeer a, WeakPeer b) {
  const auto sa = a.lock();
  const auto sb = b.lock();
  return (sa || sb) && (sa == sb);
}

}  // namespace hornet::node::net
