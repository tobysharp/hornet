#pragma once

#include <chrono>

#include "message/getheaders.h"
#include "message/headers.h"
#include "message/verack.h"
#include "node/broadcaster.h"
#include "node/inbound_handler.h"
#include "protocol/constants.h"

namespace hornet::node {

// Class for managing initial block download
class SyncManager : public InboundHandler {
 public:
  SyncManager(Broadcaster& broadcaster) : InboundHandler(&broadcaster) {}

  void OnHandshakeCompleted(std::shared_ptr<net::Peer> peer) {
    {
        const std::shared_ptr<net::Peer> sync = sync_.lock();
        if (sync && !sync->IsDropped()) return;  // We already have a sync peer
    }
    // Adopt a new peer to use for timechain sync requests
    sync_ = peer;

    // Send a message requesting headers (example only).
    message::GetHeaders getheaders(peer->GetCapabilities().GetVersion());
    getheaders.AddLocatorHash(protocol::kGenesisHash);
    Send(std::move(getheaders));
  }

  virtual void Visit(const message::Verack&) override {
    if (GetPeer()->GetHandshake().IsComplete())
        OnHandshakeCompleted(GetPeer());
  }

  virtual void Visit(const message::Headers& headers) override {
    // TODO: Request Tracking:
    // When we send outbound request messages like getheaders, ping, etc.,
    // when the outbound message is serialized, instead of deleting it, we
    // add it to a request tracking map, by peer. Then, when a request response
    // message like headers, pong, etc. arrives, we search in the tracking map
    // for a matching request issued to the same peer. If found, we remove the
    // request from the map and continue to process the response. Otherwise, we
    // ignore the inbound message, or penalize/disconnect the peer. 
    // In protocol::Message, we add
    //      virtual bool IsTrackedRequest() const { return false; }
    //      virtual bool IsMatchingRequest(const Message* request) const { return false; }
    // And we add a new RequestTracker class with, e.g.
    //      void Track(const OutboundMessage&);
    //      bool Match(const InboundMessage&);
    // However, we will defer implementing this until after header sync and validation.
    // For now, we will assume that if the message comes from our sync peer, it's good.
    if (!IsSyncPeer()) return;


  }

 private:
  std::shared_ptr<net::Peer> GetSync() const {
    return sync_.lock();
  }
  bool IsSyncPeer() const {
    const auto sync = GetSync();
    return sync && (sync == GetPeer());
  }
  template <typename T, typename... Args>
  void Send(Args... args) {
    if (const auto sync = sync_.lock())
        broadcaster_->SendMessage<T>(sync, std::forward<Args>(args)...);
  }
  template <typename T>
  void Send(T msg) {
    if (const auto sync = sync_.lock())
        broadcaster_->SendMessage<T>(sync, std::make_unique<T>(std::move(msg)));
  }

  // The peer used for timechain synchronization requests.
  std::weak_ptr<net::Peer> sync_;
};

}  // namespace hornet::node
