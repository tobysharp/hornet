#pragma once

#include <atomic>
#include <queue>

#include "net/peer.h"
#include "net/peer_manager.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "node/outbound_message.h"
#include "node/processor.h"
#include "protocol/constants.h"
#include "protocol/factory.h"

namespace hornet::node {

class Engine : public Broadcaster {
 public:
  Engine(protocol::Magic magic);

  void RunMessageLoop(int64_t timeout_ms = -1);

  void Abort() {
    abort_ = true;
  }

  std::shared_ptr<net::Peer> AddOutboundPeer(const std::string& host, uint16_t port);

  virtual void SendToOne(std::shared_ptr<net::Peer> peer, OutboundMessagePtr msg) override;
  virtual void SendToAll(OutboundMessagePtr msg) override;

 private:
  void ReadSocketsToBuffers();
  void ParseBuffersToMessageQueues();
  void ManagePeers();
  void ProcessMessages();

  protocol::Magic magic_;
  net::PeerManager peers_;
  std::atomic<bool> abort_ = false;

  std::queue<PeerPtr> peers_for_parsing_;
  protocol::Factory factory_;

  std::queue<InboundMessage> inbox_;

  std::unique_ptr<Processor> processor_;

  using OutboundMessageQueue = std::deque<OutboundMessagePtr>;
  using Outbox = std::map<PeerPtr, OutboundMessageQueue, std::owner_less<PeerPtr>>;
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
};

}  // namespace hornet::node
