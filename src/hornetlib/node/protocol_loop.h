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

  void ReadToInbox();
  void WriteFromOutbox();
  void NotifyEvents();
  void NotifyHandshake();
  void NotifyLoop();
  void ReadSocketsToBuffers(net::PeerManager& peers, std::queue<net::WeakPeer>& peers_for_parsing);
  void ParseBuffersToMessages(std::queue<net::WeakPeer>& peers_for_parsing, Inbox& inbox);
  void ProcessMessages();
  void FrameMessagesToBuffers(Outbox& outbox);
  void WriteBuffersToSockets(net::PeerManager& peers);
  void ManagePeers();

  net::PeerManager& peers_;
  std::atomic<bool> abort_ = false;
  std::queue<net::WeakPeer> peers_for_parsing_;
  Inbox inbox_;
  Outbox outbox_;
  std::vector<EventHandler*> event_handlers_;
  std::unordered_set<net::PeerId> handshake_complete_;

  // The maximum number of milliseconds to wait per loop iteration for data to arrive.
  // Smaller values lead to more spinning in the message loop during inactivity, while
  // larger values can lead to delays in servicing other stages of the loop pipeline.
  static constexpr int kPollReadTimeoutMs = 2;  // 2 ms

  // The maximum number of bytes to read per peer per frame.
  static constexpr size_t kMaxReadBytesPerFrame = 64 * 1024;  // 64 KiB

  // The maximum number of messages to parse per peer per frame.
  static constexpr size_t kMaxParsedMessagesPerFrame = 1;

  // The maximum number of messages to process per frame.
  static constexpr size_t kMaxProcessedMessagesPerFrame = 16;

  static constexpr int kPollWriteTimeoutMs = 50;  // 50 ms
  static constexpr size_t kMaxWriteBuffersPerPeer = 10;
};

}  // namespace hornet::node
