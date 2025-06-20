#pragma once

#include <chrono>

#include "data/header_sync.h"
#include "data/timechain.h"
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
  SyncManager(data::Timechain& timechain, Broadcaster& broadcaster)
      : InboundHandler(&broadcaster), headers_(timechain.Headers()) {}
  SyncManager() = delete;

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
    if (GetPeer()->GetHandshake().IsComplete()) OnHandshakeCompleted(GetPeer());
  }

  virtual void Visit(const message::Headers& headers) override {
    // TODO: [HOR-20: Request tracking]
    // (https://linear.app/hornet-node/issue/HOR-20/request-tracking)
    if (!IsSyncPeer()) return;

    const auto getheaders = headers_.OnHeaders(
        GetSync(), headers,
        [](net::PeerId id, const protocol::BlockHeader&, consensus::HeaderError error) {
          if (auto peer = net::Peer::FromId(id)) {
            LogWarn() << "Header validation failed with error " << static_cast<int>(error)
                      << ", dropping peer.";
            peer->Drop();
          }
        });

    // Request the next batch of headers if appropriate.
    if (getheaders) Send(std::move(*getheaders));
  }

  const data::HeaderSync& GetHeaderSync() const {
    return headers_;
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

  std::weak_ptr<net::Peer> sync_;  // The peer used for timechain synchronization requests.
  data::HeaderSync headers_;
};

}  // namespace hornet::node
