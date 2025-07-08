// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <atomic>
#include <queue>

#include "hornetlib/data/timechain.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/net/peer_manager.h"
#include "hornetlib/node/broadcaster.h"
#include "hornetlib/node/event_handler.h"
#include "hornetlib/node/protocol_loop.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/framer.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetlib/protocol/parser.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/timeout.h"

namespace hornet::node {

void ProtocolLoop::SendToOne(net::SharedPeer peer, std::unique_ptr<protocol::Message> message) {
  if (peer && !peer->IsDropped()) {
    const SerializationMemoPtr memo = std::make_shared<SerializationMemo>(std::move(message));
    outbox_[peer].push_back(memo);  // Creates queue if previously non-existent
    LogInfo() << "Sent: peer = " << peer << ", msg = " << *memo;
  }
}

void ProtocolLoop::SendToAll(std::unique_ptr<protocol::Message> message) {
  const SerializationMemoPtr memo = std::make_shared<SerializationMemo>(std::move(message));
  for (auto pair : outbox_) {
    pair.second.push_back(memo);
    LogInfo() << "Sent: peer = " << *pair.first.lock() << ", message = " << *memo;
  }
}

std::shared_ptr<net::Peer> ProtocolLoop::AddOutboundPeer(const std::string& host, uint16_t port) {
  // TODO: Pass outbound direction to AddPeer also
  const std::shared_ptr<net::Peer> peer = peers_.AddPeer(host, port /*, Peer::Direction::Outbound*/);
  for (EventHandler* handler : event_handlers_)
    handler->OnPeerConnect(peer);
  return peer;
}

void ProtocolLoop::RunMessageLoop(BreakCondition condition /* = BreakOnTimeout{} */) {
  // We design the message loop in discrete stages with well-defined boundaries between
  // each, so that the various stages can be executed in parallel in pipeline fashion,
  // for example so that last frame's data is being parsed while this frame's data is
  // being read, etc. Beyond this task parallelization, much of the work within each task
  // can also be parallelized and split up among a pool of worker threads for efficiency.

  while (!abort_ && !condition(*this)) {
    ReadToInbox();

    // Message Dispatch / Processing.
    ProcessMessages();

    WriteFromOutbox();

    NotifyLoop();
  }
}

void ProtocolLoop::ReadToInbox() {
    // 1. Reading.
    ReadSocketsToBuffers(peers_, peers_for_parsing_);

    // 2. Parsing + Deserializing.
    ParseBuffersToMessages(peers_for_parsing_, inbox_);
}

void ProtocolLoop::WriteFromOutbox() {
      // 4. Serializing + Framing.
    FrameMessagesToBuffers(outbox_);

    // 5. Writing.
    WriteBuffersToSockets(peers_);
}

void ProtocolLoop::NotifyLoop() {
  for (EventHandler* handler : event_handlers_)
    handler->OnLoop(peers_);
}

// Determine which peers' sockets have data for reading, and iterate over them,
// reading socket data into local buffers. This can be parallelized over the peers
// if preferable.
void ProtocolLoop::ReadSocketsToBuffers(net::PeerManager& peers, std::queue<net::WeakPeer>& peers_for_parsing) {
  for (const auto& peer : peers.PollRead(kPollReadTimeoutMs)) {
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
void ProtocolLoop::ParseBuffersToMessages(std::queue<net::WeakPeer>& peers_for_parsing, Inbox& inbox) {
  protocol::Parser parser;
  while (!peers_for_parsing.empty()) {
    const std::shared_ptr<net::Peer> peer = peers_for_parsing.front().lock();
    peers_for_parsing.pop();
    if (!peer) continue;

    try {
      const auto& factory = protocol::MessageFactory::Default();
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
        if (auto msg = factory.Create(parsed.header.command)) {
          // Deserialize the message from the payload.
          encoding::Reader reader{parsed.payload};
          msg->Deserialize(reader);

          // Writes the metadata into the message.
          msg->SetEnvelope({
            .direction = protocol::Message::Direction::Inbound,
            .peer_id = peer->GetId(),
            .timestamp = std::chrono::system_clock::now()
          });

          // Add the deserialized message to the queue for dispatch and processing.
          inbox.push(std::move(msg));
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
void ProtocolLoop::ProcessMessages() {
  for (size_t processed_count = 0;
       !inbox_.empty() && processed_count < kMaxProcessedMessagesPerFrame; ++processed_count) {
    std::unique_ptr<protocol::Message> message = std::move(inbox_.front());
    inbox_.pop();
    try {
      LogInfo() << "Received: " << *message;
      for (EventHandler* handler : event_handlers_)
        message->Notify(*handler);  // Double-dispatch via virtual visitor pattern.
    } catch (std::exception& e) {
      // On unexpected exception, treat as protocol violation: close socket.
      if (const auto envelope = message->GetEnvelope())
        if (const auto peer = peers_.GetRegistry().FromId(envelope->peer_id))
          peer->Drop();
    }
  }
}

// Iterates over peers, and over queued work per peer. While each peer has space available and work
// items waiting, serialize the message if not already done, then queue the serialized buffer to the
// peer's output.
void ProtocolLoop::FrameMessagesToBuffers(Outbox& outbox) {
  for (auto& [wpeer, queue] : outbox) {
    const auto peer = wpeer.lock();
    if (!peer || peer->IsDropped()) continue;
    try {
      size_t queue_size = peer->GetConnection().QueuedWriteBufferCount();
      while (!queue.empty() && queue_size < kMaxWriteBuffersPerPeer) {
        const auto memo = std::move(queue.front());
        queue.pop_front();  // Pop now so that if we throw an exception during processing, we don't
                            // repeat the error next frame.
        peer->GetConnection().EnqueueWrite(memo->GetSerializedBuffer());
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
void ProtocolLoop::WriteBuffersToSockets(net::PeerManager& peers) {
  const auto select = [](const net::Peer& peer) {
    return peer.GetConnection().QueuedWriteBufferCount() > 0;
  };

  [[maybe_unused]] size_t bytes_written = 0;
  for (const auto& peer : peers.PollWrite(kPollWriteTimeoutMs, select)) {
    if (peer) bytes_written += peer->GetConnection().ContinueWrite();
  }
}

void ProtocolLoop::ManagePeers(net::PeerManager& peers) {
  // Removes all the peers whose sockets have been closed.
  peers.RemoveClosedPeers();

  // TODO: Other bookkeeping and network tasks.
}

}  // namespace hornet::node