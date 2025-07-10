// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <atomic>
#include <deque>
#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>

#include "hornetlib/data/timechain.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/net/peer_registry.h"
#include "hornetlib/net/peer_manager.h"
#include "hornetlib/node/broadcaster.h"
#include "hornetlib/node/event_handler.h"
#include "hornetlib/node/serialization_memo.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/util/timeout.h"

namespace hornet::node {

class ProtocolLoop : public Broadcaster {
 public:
  struct BreakOnTimeout {
    BreakOnTimeout(int timeout_ms = 0) : timeout_(timeout_ms) {}
    bool operator()(const ProtocolLoop&) const { return timeout_.IsExpired(); }
    util::Timeout timeout_;
  };
  using BreakCondition = std::function<bool(const ProtocolLoop&)>;
 
  ProtocolLoop(net::PeerManager& peers) : peers_(peers) {}

  void AddEventHandler(EventHandler* handler) {
    Assert(handler != nullptr);
    handler->SetBroadcaster(*this);
    handler->SetPeerRegistry(peers_.GetRegistry());
    event_handlers_.push_back(handler);
  }
  
  std::shared_ptr<net::Peer> AddOutboundPeer(const std::string& host, uint16_t port);

  void RunMessageLoop(BreakCondition condition = BreakOnTimeout{});

  void Abort() {
    abort_ = true;
  }

  virtual void SendToOne(std::shared_ptr<net::Peer> peer, std::unique_ptr<protocol::Message> message) override;
  virtual void SendToAll(std::unique_ptr<protocol::Message> message) override;

 private:
  using Inbox = std::queue<std::unique_ptr<protocol::Message>>;
  using SharedOutboundMessage = std::shared_ptr<SerializationMemo>;
  using OutboundMessageQueue = std::deque<SharedOutboundMessage>;
  using OutboxKey = std::weak_ptr<net::Peer>;
  using OutboxCompare = std::owner_less<OutboxKey>;
  using Outbox = std::map<OutboxKey, OutboundMessageQueue, OutboxCompare>;

  net::PeerManager::PollResult PollReadWrite();
  void ReadToInbox(std::span<net::SharedPeer> read);
  void WriteFromOutbox(std::span<net::SharedPeer> write);
  void NotifyEvents();
  void NotifyHandshake();
  void NotifyLoop();
  static void ReadSocketsToBuffers(std::span<net::SharedPeer> read, std::queue<net::WeakPeer>& peers_for_parsing);
  static void ParseBuffersToMessages(std::queue<net::WeakPeer>& peers_for_parsing, Inbox& inbox);
  void ProcessMessages();
  static void FrameMessagesToBuffers(Outbox& outbox);
  static int WriteBuffersToSockets(std::span<net::SharedPeer> write);
  void Cleanup();

  net::PeerManager& peers_;
  std::atomic<bool> abort_ = false;
  std::queue<net::WeakPeer> peers_for_parsing_;
  Inbox inbox_;
  Outbox outbox_;
  std::vector<EventHandler*> event_handlers_;
  std::unordered_set<net::PeerId> handshake_complete_;

  // Loop tuning parameters — control per-peer and per-frame limits

  // Maximum number of bytes to read per peer in a single frame.
  // Protects against peers flooding us with large or continuous data.
  static constexpr int kMaxReadBytesPerFrame = 64 * 1024;  // 64 KiB

  // Maximum number of messages to parse per peer in a single frame.
  // Prevents noisy peers from overwhelming the parser with many tiny messages.
  static constexpr int kMaxParsedMessagesPerFrame = 5;

  // Maximum processing time allowed per frame across all peers.
  // Prevents processing inbound messages from starving timely responses.
  static constexpr int kMaxProcessMsPerFrame = 50;

  // Maximum number of pending write buffers per peer.
  // Prevents a peer from queuing unbounded outbound data and consuming excessive memory.
  static constexpr int kMaxWriteBuffersPerPeer = 10;

  // Maximum time to block when polling if we don't have any messages queued already.
  // Balances between allowing idle time and being responsive to aborts. 
  static constexpr int kMaxPollTimeoutMs = 100;
};

}  // namespace hornet::node
