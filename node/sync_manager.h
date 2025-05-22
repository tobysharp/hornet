#pragma once

#include "message/getheaders.h"
#include "message/headers.h"
#include "message/verack.h"
#include "node/broadcaster.h"
#include "node/inbound_handler.h"

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
