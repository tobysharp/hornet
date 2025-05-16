#include <atomic>
#include <queue>

#include "message/registry.h"
#include "net/peer.h"
#include "net/peer_manager.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "node/outbound_message.h"
#include "node/processor.h"
#include "protocol/constants.h"
#include "protocol/factory.h"
#include "protocol/parser.h"

namespace hornet::node {

class ProtocolThread : public Broadcaster {
 public:
  ProtocolThread(protocol::Magic magic);

  void MessageLoop();

  void Abort() {
    abort_ = true;
  }

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

  Processor processor_;

  using OutboundMessageQueue = std::deque<OutboundMessagePtr>;
  using Outbox = std::map<PeerPtr, OutboundMessageQueue>;  
  Outbox outbox_;

  // The maximum number of milliseconds to wait per loop iteration for data to arrive.
  // Smaller values lead to more spinning in the message loop during inactivity, while
  // larger values can lead to delays in servicing other stages of the loop pipeline.
  static constexpr int kPollReadTimeoutMs = 2;  // 2 ms

  // The maximum number of bytes to read per peer per frame.
  static constexpr size_t kMaxReadBytesPerFrame = 64 * 1024;  // 64 KiB

  // The maximum number of messages to parse per peer per frame.
  static constexpr size_t kMaxParsedMessagesPerFrame = 1;
};

ProtocolThread::ProtocolThread(protocol::Magic magic)
    : magic_(magic), factory_(message::CreateMessageFactory()), processor_(factory_, *this) {}

void ProtocolThread::SendToOne(std::shared_ptr<net::Peer> peer, OutboundMessagePtr msg) {
  const auto it = outbox_.find(std::weak_ptr<net::Peer>(peer));
  if (it == outbox_.end()) {
    // The peer's outbound message queue was not found in the map.
    throw std::runtime_error{"Peer not found in the outbox."};
  }
  it->second.push_back(msg);
}

void ProtocolThread::SendToAll(OutboundMessagePtr msg) {
  for (auto pair : outbox_) {
    pair.second.push_back(msg);
  }
}

void ProtocolThread::MessageLoop() {
  // We design the message loop in discrete stages with well-defined boundaries between
  // each, so that the various stages can be executed in parallel in pipeline fashion,
  // for example so that last frame's data is being parsed while this frame's data is
  // being read, etc. Beyond this task parallelization, much of the work within each task
  // can also be parallelized and split up among a pool of worker threads for efficiency.

  while (!abort_) {
    // 1. Reading.
    ReadSocketsToBuffers();

    // 2. Parsing.
    ParseBuffersToMessageQueues();

    // 3. Message Dispatch / Processing.
    ProcessMessages();

    // 4. Framing.
    // 5. Writing.
    // 6. Connection Management & Bookkeeping.
    ManagePeers();
  }
}

// Determine which peers' sockets have data for reading, and iterate over them,
// reading socket data into local buffers. This can be parallelized over the peers
// if preferable.
void ProtocolThread::ReadSocketsToBuffers() {
  for (const auto peer : peers_.PollRead(kPollReadTimeoutMs)) {
    // Reads bytes from a peer's socket to its internal memory buffer.
    const size_t bytes = peer->GetConnection().ReadToBuffer(kMaxReadBytesPerFrame);

    if (bytes == 0) {
      // Either the socket was closed, or else the socket is in non-blocking mode
      // and there was no data actually readable. The latter case would be very
      // strange, and worth noting, but not necessarily an error. In either case,
      // we don't need to do anything here because later we will remove any closed
      // sockets from the peer manager before the next iteration.
      continue;
    }

    // Add this peer to the queue for parsing.
    peers_for_parsing_.push(peer);
  }
  // All the socket reading has now been done for this frame
}

// Visit each peer buffer with new data recently arrived, and look to see whether
// there exists one or more whole messages ready for parsing. If so, parse and store
// the message in a queue, then eat the bytes in the peer buffer.
void ProtocolThread::ParseBuffersToMessageQueues() {
  protocol::Parser parser(magic_);
  while (!peers_for_parsing_.empty()) {
    const std::shared_ptr<net::Peer> peer = peers_for_parsing_.front().lock();
    peers_for_parsing_.pop();
    if (!peer) continue;

    bool continue_later = true;
    try {
      for (size_t count = 0; count < kMaxParsedMessagesPerFrame; ++count) {
        const auto unparsed = peer->GetConnection().PeekBufferedData();
        if (!parser.IsCompleteMessage(unparsed)) {
          // There are no more complete messages to be parsed for this peer.
          continue_later = false;
          break;
        }

        // Parse the message, validating the header data.
        const auto parsed = parser.Parse(unparsed);

        // Eat the parsed bytes from the peer buffer.
        peer->GetConnection().ConsumeBufferedData(protocol::kHeaderLength + parsed.payload.size());

        // Instantiate a protocol::Message object of the correct derived type.
        auto msg = factory_.Create(parsed.header.command);

        // Deserialize the message from the payload.
        encoding::Reader reader{parsed.payload};
        msg->Deserialize(reader);

        // Add the deserialized message to the queue for dispatch and processing.
        inbox_.push({peer, std::move(msg)});
      }
    } catch (std::exception& e) {
      // If any peer-specific behavior throws, we will defensively drop the connection,
      // marking the peer for removal. This also clears the connection's read buffer,
      // preventing looping on poisoned data.
      // TODO: Log error
      peer->GetConnection().Drop();
      continue_later = false;
    }
    // Peer may have more complete messages — requeue for next frame
    if (continue_later) peers_for_parsing_.push(peer);
  }
}

void ProtocolThread::ProcessMessages() {

}

void ProtocolThread::ManagePeers() {
  // Removes all the peers whose sockets have been closed.
  peers_.RemoveClosedPeers();

  // TODO: Other bookkeeping and network tasks.
}


}  // namespace hornet::node