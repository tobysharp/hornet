#include <atomic>
#include <queue>

#include "data/timechain.h"
#include "message/registry.h"
#include "net/peer.h"
#include "net/peer_manager.h"
#include "node/broadcaster.h"
#include "node/engine.h"
#include "node/inbound_message.h"
#include "node/outbound_message.h"
#include "node/processor.h"
#include "protocol/constants.h"
#include "protocol/factory.h"
#include "protocol/framer.h"
#include "protocol/parser.h"
#include "util/log.h"
#include "util/timeout.h"

namespace hornet::node {

Engine::Engine(data::Timechain& timechain, protocol::Magic magic)
    : Broadcaster(), timechain_(timechain), magic_(magic), factory_(message::CreateMessageFactory()) {
  processor_.emplace(factory_, *this);
  sync_manager_.emplace(timechain_, *this);
}

void Engine::SendToOne(const std::shared_ptr<net::Peer>& peer, OutboundMessage&& msg) {
  if (!peer->IsDropped()) {
    const SerializationMemoPtr memo = std::make_shared<SerializationMemo>(std::move(msg));
    outbox_[peer].emplace_back(memo);  // Creates queue if previously non-existent
    LogInfo() << "Sent: peer = " << *peer << ", msg = " << memo->GetOutbound();
  }
}

void Engine::SendToAll(OutboundMessage&& msg) {
  const SerializationMemoPtr memo = std::make_shared<SerializationMemo>(std::move(msg));
  for (auto pair : outbox_) {
    pair.second.emplace_back(memo);
    LogInfo() << "Sent: peer = " << *pair.first.lock() << ", msg = " << memo->GetOutbound();
  }
}

std::shared_ptr<net::Peer> Engine::AddOutboundPeer(const std::string& host, uint16_t port) {
  // TODO: Pass outbound direction to AddPeer also
  const auto peer = peers_.AddPeer(host, port /*, Peer::Direction::Outbound*/);
  processor_->InitiateHandshake(peer);
  return peer;
}

void Engine::RunMessageLoop(BreakCondition condition /* = BreakOnTimeout{} */) {
  // We design the message loop in discrete stages with well-defined boundaries between
  // each, so that the various stages can be executed in parallel in pipeline fashion,
  // for example so that last frame's data is being parsed while this frame's data is
  // being read, etc. Beyond this task parallelization, much of the work within each task
  // can also be parallelized and split up among a pool of worker threads for efficiency.

  while (!abort_ && !condition(*this)) {
    // 1. Reading.
    ReadSocketsToBuffers(peers_, peers_for_parsing_);

    // 2. Parsing + Deserializing.
    ParseBuffersToMessages(peers_for_parsing_, inbox_);

    // 3. Message Dispatch / Processing.
    ProcessMessages(inbox_);

    // 4. Serializing + Framing.
    FrameMessagesToBuffers(outbox_);

    // 5. Writing.
    WriteBuffersToSockets(peers_);

    // 6. Connection Management & Bookkeeping.
    ManagePeers(peers_);
  }
}

// Determine which peers' sockets have data for reading, and iterate over them,
// reading socket data into local buffers. This can be parallelized over the peers
// if preferable.
void Engine::ReadSocketsToBuffers(net::PeerManager& peers, std::queue<PeerPtr>& peers_for_parsing) {
  for (const auto peer : peers.PollRead(kPollReadTimeoutMs)) {
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
    peers_for_parsing.push(peer);
  }
  // All the socket reading has now been done for this frame
}

// Visit each peer buffer with new data recently arrived, and look to see whether
// there exists one or more whole messages ready for parsing. If so, parse and store
// the message in a queue, then eat the bytes in the peer buffer.
void Engine::ParseBuffersToMessages(std::queue<PeerPtr>& peers_for_parsing, Inbox& inbox) {
  protocol::Parser parser(magic_);
  while (!peers_for_parsing.empty()) {
    const std::shared_ptr<net::Peer> peer = peers_for_parsing.front().lock();
    peers_for_parsing.pop();
    if (!peer) continue;

    try {
      bool continue_later = true;
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
        if (auto msg = factory_.Create(parsed.header.command)) {
          // Deserialize the message from the payload.
          encoding::Reader reader{parsed.payload};
          msg->Deserialize(reader);

          // Add the deserialized message to the queue for dispatch and processing.
          inbox.push({peer, std::move(msg)});
        } else {
          // Unrecognized message command.
        }
      }
      // Allow the connection's input buffer to be trimmed
      peer->GetConnection().TrimBufferedData();
      // Peer may have more complete messages — requeue for next frame
      if (continue_later) peers_for_parsing.push(peer);
    } catch (std::exception& e) {
      // If any peer-specific behavior throws, we will defensively drop the connection,
      // marking the peer for removal. This also clears the connection's read buffer,
      // preventing looping on poisoned data.
      // TODO: Log error
      peer->Drop();
   }
  }
}

// Pull messages from the inbound queue and dispatch each one in turn to the processor.
// OPT: A future optimization could be to distribute work over parallel threads where each
// concurrent thread represents a different peer.
void Engine::ProcessMessages(Inbox& inbox) {
  for (size_t processed_count = 0;
       !inbox.empty() && processed_count < kMaxProcessedMessagesPerFrame; ++processed_count) {
    InboundMessage inbound = std::move(inbox.front());
    inbox.pop();
    try {
      LogInfo() << "Received: " << inbound;
      processor_->Process(inbound);
      sync_manager_->Process(inbound);
    } catch (std::exception& e) {
      // On unexpected exception, treat as protocol violation: close socket.
      if (auto peer = inbound.GetPeer()) peer->Drop();
    }
  }
}

// Iterates over peers, and over queued work per peer. While each peer has space available and work
// items waiting, serialize the message if not already done, then queue the serialized buffer to the
// peer's output.
void Engine::FrameMessagesToBuffers(Outbox& outbox) {
  for (auto& [wpeer, queue] : outbox) {
    const auto peer = wpeer.lock();
    if (!peer || peer->IsDropped()) continue;
    try {
      size_t queue_size = peer->GetConnection().QueuedWriteBufferCount();
      while (!queue.empty() && queue_size < kMaxWriteBuffersPerPeer) {
        const auto memo = queue.front();
        queue.pop_front();  // Pop now so that if we throw an exception during processing, we don't
                            // repeat the error next frame.
        peer->GetConnection().EnqueueWrite(memo->GetSerializedBuffer(magic_));
        ++queue_size;
      }
    } catch (std::exception& e) {
      // Something went wrong -- defensively drop the connection.
      peer->Drop();
    }
  }
}

// Loops over all active peers that have binary data waiting in buffers ready to be sent,
// and send the maximum amount per peer without risking blocking.
void Engine::WriteBuffersToSockets(net::PeerManager& peers) {
  const auto select = [](const net::Peer& peer) {
    return peer.GetConnection().QueuedWriteBufferCount() > 0;
  };

  [[maybe_unused]] size_t bytes_written = 0;
  for (const auto peer : peers.PollWrite(kPollWriteTimeoutMs, select)) {
    if (peer) bytes_written += peer->GetConnection().ContinueWrite();
  }
}

void Engine::ManagePeers(net::PeerManager& peers) {
  // Removes all the peers whose sockets have been closed.
  peers.RemoveClosedPeers();

  // TODO: Other bookkeeping and network tasks.
}

}  // namespace hornet::node