#pragma once

#include <atomic>
#include <deque>
#include <optional>
#include <queue>

#include "net/peer.h"
#include "net/peer_manager.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "node/outbound_message.h"
#include "node/processor.h"
#include "node/serialization_memo.h"
#include "protocol/constants.h"
#include "protocol/factory.h"
#include "util/timeout.h"

namespace hornet::node {

class Engine : public Broadcaster {
 public:
  struct BreakOnTimeout {
    BreakOnTimeout(int timeout_ms = 0) : timeout_(timeout_ms) {}
    bool operator()(const Engine&) const { return timeout_.IsExpired(); }
    util::Timeout timeout_;
  };
  using BreakCondition = std::function<bool(const Engine&)>;
 
  Engine(protocol::Magic magic);
  void RunMessageLoop(BreakCondition condition = BreakOnTimeout{});

  void Abort() {
    abort_ = true;
  }

  std::shared_ptr<net::Peer> AddOutboundPeer(const std::string& host, uint16_t port);

  virtual void SendToOne(const std::shared_ptr<net::Peer>& peer, OutboundMessage&& msg) override;
  virtual void SendToAll(OutboundMessage&& msg) override;

 private:
  using Inbox = std::queue<InboundMessage>;
  using OutboundMessageQueue = std::deque<SerializationMemoPtr>;
  using Outbox = std::map<PeerPtr, OutboundMessageQueue, std::owner_less<PeerPtr>>;

  void ReadSocketsToBuffers(net::PeerManager& peers, std::queue<PeerPtr>& peers_for_parsing);
  void ParseBuffersToMessages(std::queue<PeerPtr>& peers_for_parsing, Inbox& inbox);
  void ProcessMessages(Inbox& inbox, Processor& processor);
  void FrameMessagesToBuffers(Outbox& outbox);
  void WriteBuffersToSockets(net::PeerManager& peers);
  void ManagePeers(net::PeerManager& peers);

  protocol::Magic magic_;
  net::PeerManager peers_;
  std::atomic<bool> abort_ = false;
  std::queue<PeerPtr> peers_for_parsing_;
  protocol::Factory factory_;
  Inbox inbox_;
  std::optional<Processor> processor_;
  Outbox outbox_;

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
