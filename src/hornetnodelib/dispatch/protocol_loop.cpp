// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <atomic>
#include <chrono>
#include <queue>
#include <span>
#include <thread>
#include <vector>

#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/framer.h"
#include "hornetlib/protocol/message_factory.h"
#include "hornetlib/protocol/parser.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/timeout.h"
#include "hornetnodelib/dispatch/broadcaster.h"
#include "hornetnodelib/dispatch/event_handler.h"
#include "hornetnodelib/dispatch/protocol_loop.h"
#include "hornetnodelib/net/peer.h"
#include "hornetnodelib/net/peer_manager.h"

namespace hornet::node::dispatch {

void ProtocolLoop::SendToOne(net::SharedPeer peer, std::unique_ptr<protocol::Message> message) {
  if (peer && !peer->IsDropped()) {
    const SerializationMemoPtr memo = std::make_shared<SerializationMemo>(std::move(message));
    outbox_[peer].push_back(memo);  // Creates queue if previously non-existent
    LogInfo() << "Sent: peer = " << *peer << ", msg = " << *memo;
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
  const std::shared_ptr<net::Peer> peer =
      peers_.AddPeer(host, port /*, Peer::Direction::Outbound*/);
  for (EventHandler* handler : event_handlers_) handler->OnPeerConnect(peer);
  return peer;
}

void ProtocolLoop::RunMessageLoop(BreakCondition condition /* = BreakOnTimeout{} */) {
  // We design the message loop in discrete stages with well-defined boundaries between
  // each, so that the various stages can be executed in parallel in pipeline fashion,
  // for example so that last frame's data is being parsed while this frame's data is
  // being read, etc. Beyond this task parallelization, much of the work within each task
  // can also be parallelized and split up among a pool of worker threads for efficiency.

  NotifyLoop();
  while (!abort_ && !condition()) {
    // Poll.
    auto polled = PollReadWrite();
    // Read and parse.
    ReadToInbox(polled.read);
    // Dispatch.
    ProcessMessages();
    // Frame and write.
    WriteFromOutbox(polled.write);
    // Notify.
    NotifyEvents();
    // Bookkeeping.
    Cleanup();
  }
}

net::PeerManager::PollResult ProtocolLoop::PollReadWrite() {
  // Determines whether there is remaining parsing or processing work to be done left over from
  // a previous frame which, if not prioritized, could lead us to block unproductively on polling.
  const bool outbox_pending = std::any_of(outbox_.begin(), outbox_.end(),
                                          [](const auto& entry) { return !entry.second.empty(); });
  const bool backlog = !peers_for_parsing_.empty() || !inbox_.empty() || outbox_pending;
  if (backlog)
    LogDebug() << "ProtocolLoop::PollReadWrite non-blocking poll due to backlog.";

  // If we already have pending input or output to process, don't block on poll;
  // instead, iterate immediately to reduce latency. This is expected during normal operation.
  const int timeout_ms = backlog ? 0 : kMaxPollTimeoutMs;

  // TODO: Note that we have some work to do in being resilient to DoS attacks which could keep
  // us spinning with a permanent backlog. We may want to track backlog into a metric over time,
  // but ultimately we need to move to per-peer input queues and appropriate peer scoring to
  // robustly secure against malicious peers.
  // https://linear.app/hornet-node/issue/HOR-39/per-peer-inbox-queues

  // TODO: Note that if our connection queues are currently empty and there's no work to be done
  // we will enter the poll with a timeout of kMaxPollTimeoutMs, and if another thread broadcasts an
  // outbound message to our outbox during our blocking period, we will not wake up to service that
  // newly arriving work item. We will only discover the work when the poll times out.

  auto ready = peers_.PollReadWrite(timeout_ms, [](const net::Peer& peer) {
    return peer.GetConnection().QueuedWriteBufferCount() > 0;
  });

  // If there were no connected peers to poll, then we should sleep to prevent a tight spin loop.
  // When we awake, if there are new peers connected, we will start servicing them in the next iteration.
  // This is expected to be an edge case (no connected peers). But if the latency of this sleep becomes
  // an issue, we can add a condition variable so new peers force immediate wake-up from this sleep.
  if (ready.empty) {
    LogDebug() << "ProtocolLoop::PollReadWrite has nothing to poll.";
    std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms});
  }

  // Create a fast, non-cryptographic pseudo-random generator seeded with current time.
  static thread_local std::mt19937 rng{
      static_cast<unsigned long>(std::chrono::steady_clock::now().time_since_epoch().count())};

  // Shuffle read order to avoid any structural bias in message dispatching priority.
  // Maybe irrelevant when inbox is per-peer.
  std::ranges::shuffle(ready.read, rng);
  std::ranges::shuffle(ready.write, rng);
  return ready;
}

void ProtocolLoop::ReadToInbox(std::span<net::SharedPeer> read) {
  // Read from sockets.
  ReadSocketsToBuffers(read, peers_for_parsing_);

  // Parse and deserialize messages.
  ParseBuffersToMessages(peers_for_parsing_, inbox_);
}

void ProtocolLoop::WriteFromOutbox(std::span<net::SharedPeer> write) {
  // Serialize and frame messages.
  FrameMessagesToBuffers(outbox_);

  // Write to sockets.
  WriteBuffersToSockets(write);
}

void ProtocolLoop::NotifyEvents() {
  NotifyHandshake();
  NotifyLoop();
}

void ProtocolLoop::NotifyHandshake() {
  for (const auto& peer : peers_.GetRegistry().Snapshot()) {
    if (peer->IsDropped() || !peer->GetHandshake().IsComplete()) 
      continue;

    if (handshake_complete_.insert(peer->GetId()).second) {
      for (EventHandler* handler : event_handlers_) handler->OnHandshakeComplete(peer);
    }
  }
}

void ProtocolLoop::NotifyLoop() {
  for (EventHandler* handler : event_handlers_) handler->OnLoop(peers_);
}

// Determine which peers' sockets have data for reading, and iterate over them,
// reading socket data into local buffers. This can be parallelized over the peers
// if preferable.
/* static */ void ProtocolLoop::ReadSocketsToBuffers(std::span<net::SharedPeer> read,
                                                     std::queue<net::WeakPeer>& peers_for_parsing) {
  for (const auto& peer : read) {
    // Reads bytes from a peer's socket to its internal memory buffer.
    // Limit the number of bytes read per peer per frame to avoid memory pressure from bursty peers.
    const ssize_t bytes = peer->GetConnection().ReadToBuffer(kMaxReadBytesPerFrame);

    if (bytes < 0) {  // The socket was closed.
      peer->Drop();
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
/* static */ void ProtocolLoop::ParseBuffersToMessages(std::queue<net::WeakPeer>& peers_for_parsing,
                                                       Inbox& inbox) {
  protocol::Parser parser;
  while (!peers_for_parsing.empty()) {
    const net::SharedPeer peer = peers_for_parsing.front().lock();
    peers_for_parsing.pop();
    if (!peer || peer->IsDropped()) 
      continue;

    try {
      const auto& factory = protocol::MessageFactory::Default();
      bool continue_later = true;
      // Limit the number of messages parsed per peer per frame to prevent monopolization by noisy
      // peers.
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
          msg->SetEnvelope({.direction = protocol::Message::Direction::Inbound,
                            .peer_id = peer->GetId(),
                            .timestamp = std::chrono::system_clock::now()});

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
      LogWarn() << "ProtocolLoop::ParseBuffersToMessages dropping peer " << *peer << ": \""
        << e.what() << "\".";
      peer->Drop();
    }
  }
}

// Pull messages from the inbound queue and dispatch each one in turn to the processor.
// OPT: A future optimization could be to distribute work over parallel threads where each
// concurrent thread represents a different peer.
void ProtocolLoop::ProcessMessages() {
  // Limit total time spent processing messages in one frame to prevent write starvation.
  util::Timeout timeout(kMaxProcessMsPerFrame);
  while (!inbox_.empty() && !timeout.IsExpired()) {
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
  if (timeout.IsExpired() && !inbox_.empty())
    LogDebug() << "ProtocolLoop::ProcessMessages timeout expired with " << inbox_.size() << " messages in inbox.";
}

// Iterates over peers, and over queued work per peer. While each peer has space available and work
// items waiting, serialize the message if not already done, then queue the serialized buffer to the
// peer's output.
/* static */ void ProtocolLoop::FrameMessagesToBuffers(Outbox& outbox) {
  for (auto& [wpeer, queue] : outbox) {
    const auto peer = wpeer.lock();
    if (!peer || peer->IsDropped())
      continue;

    try {
      size_t queue_size = peer->GetConnection().QueuedWriteBufferCount();
      // Skip serialization if peer has reached max buffer count, preventing unbounded memory use.
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
/* static */ int ProtocolLoop::WriteBuffersToSockets(std::span<net::SharedPeer> write) {
  int bytes_written = 0;
  for (const auto& peer : write) bytes_written += peer->GetConnection().ContinueWrite();
  return bytes_written;
}

void ProtocolLoop::Cleanup() {
  // Removes all the peers whose sockets have been closed,
  // and cleans up any auxiliary associated data.
  for (const auto& peer : peers_.GetRegistry().Snapshot()) {
    if (!peer->IsDropped())
      continue;
    outbox_.erase(peer);
    handshake_complete_.erase(peer->GetId());
    for (EventHandler* handler : event_handlers_)
      handler->OnPeerDisconnect(peer);
    peers_.RemovePeer(peer);
  }

  // TODO: Other bookkeeping and network tasks.
}

}  // namespace hornet::node::dispatch