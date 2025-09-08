// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <chrono>
#include <string_view>

#include "hornetlib/data/sidecar_binding.h"
#include "hornetlib/data/timechain.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/message/getheaders.h"
#include "hornetlib/protocol/message/headers.h"
#include "hornetlib/protocol/message/verack.h"
#include "hornetlib/util/notify.h"
#include "hornetnodelib/dispatch/broadcaster.h"
#include "hornetnodelib/dispatch/event_handler.h"
#include "hornetnodelib/net/peer_registry.h"
#include "hornetnodelib/sync/block_sync.h"
#include "hornetnodelib/sync/header_sync.h"
#include "hornetnodelib/sync/sync_handler.h"
#include "hornetnodelib/sync/types.h"

namespace hornet::node::sync {

// Class for managing initial block download
class SyncManager : public dispatch::EventHandler {
 public:
  SyncManager(data::Timechain& timechain, BlockValidationBinding validation)
      : header_sync_(timechain, header_sync_handler_),
        block_sync_(timechain, validation, block_sync_handler_),
        timechain_(timechain) {}
  SyncManager() = delete;

  virtual void OnHandshakeComplete(net::SharedPeer peer) override {
    std::string notify = "Peer " + std::to_string(peer->GetId()) + " handshake completed 🤝";
    hornet::util::NotifyEvent("peers/handshake", notify, util::EventType::Info);

    {
      const net::SharedPeer sync = sync_.lock();
      if (sync && !sync->IsDropped()) return;  // We already have a sync peer
    }
    // Adopt a new peer to use for timechain sync requests
    sync_ = peer;

    // Send a message requesting headers (example only).
    header_sync_.StartSync(sync_);
  }

  virtual void OnMessage(const protocol::message::Headers& headers) override {
    // TODO: [HOR-20: Request tracking]
    // (https://linear.app/hornet-node/issue/HOR-20/request-tracking)
    if (!IsSyncPeer(headers)) return;

    // Pass the headers message to the HeaderSync object.
    header_sync_.OnHeaders(sync_, headers);
  }

  virtual void OnMessage(const protocol::message::Block& block) override {
    if (!IsSyncPeer(block)) return;

    // Pass the block message to the BlockSync object.
    block_sync_.OnBlock(GetSync(), block);
  }

  const HeaderSync& GetHeaderSync() const {
    return header_sync_;
  }

 protected:
  // Called by HeaderSync or BlockSync when a validation occurred. Drops the sync peer.
  void OnSyncError(net::WeakPeer weak, std::string_view error) {
    LogWarn() << error;
    if (const auto peer = weak.lock()) peer->Drop();
  }
  virtual bool OnSyncRequest(net::WeakPeer weak, std::unique_ptr<protocol::Message> message) {
    if (const auto peer = weak.lock()) {
      broadcaster_->SendMessage(peer, std::move(message));
      return true;
    }
    return false;
  }
  // Called by HeaderSync when a peer's header validation is up-to-date.
  virtual void OnHeaderSyncComplete(net::WeakPeer weak) {
    Assert(weak == sync_);
    // TODO: When we support multiple peers, this will be called when each peer finishes
    // syncing its headers. At this point, we can store the cumulative work on the heaviest
    // chain with the peer, so that we track how much work each peer is advertising.
    // For Initial Block Download (IBD), it's fine to use a single peer to get started from
    // the genesis up to its tip. But having done so, we probably want to sync the headers
    // from all our other peer connections too. Doing so only requires that we receive any
    // diverging headers from our current chain, if there are any. It should be fast to
    // do this with all our peers, or a subset of them. Then we can rank the peers by their
    // proof-of-work if helpful, so that we choose the best ones to use for block download.
    // Having synced headers for all the appropriate peers, we then move onto block sync.
    //
    // Since we are deferring the implementation of multiple peers, we will return here
    // later to implement the above logic. For now, we just move on to block sync.
    if (auto peer = weak.lock()) {
      std::string notify = "Peer " + std::to_string(peer->GetId()) + " synced " +
                           std::format("{:L}", timechain_.ReadHeaders()->ChainLength()) +
                           " headers 🔃";
      hornet::util::NotifyEvent("sync/headers", notify, util::EventType::Info);
      LogInfo() << "Header sync complete for peer " << peer->GetId() << ", tip height "
                << timechain_.ReadHeaders()->ChainTip()->height;
    }
    block_sync_.StartSync(weak);
  }

  class HeaderSyncHandler final : public SyncHandler {
   public:
    HeaderSyncHandler(SyncManager& manager) : manager_(manager) {}
    virtual void OnComplete(net::WeakPeer peer) override {
      manager_.OnHeaderSyncComplete(peer);
    }
    virtual bool OnRequest(net::WeakPeer peer,
                           std::unique_ptr<protocol::Message> message) override {
      return manager_.OnSyncRequest(peer, std::move(message));
    }
    virtual void OnError(net::WeakPeer peer, std::string_view error) override {
      manager_.OnSyncError(peer, error);
    }

   private:
    SyncManager& manager_;
  } header_sync_handler_ = *this;

  class BlockSyncHandler final : public SyncHandler {
   public:
    BlockSyncHandler(SyncManager& manager) : manager_(manager) {}
    virtual void OnComplete(net::WeakPeer) override {}
    virtual bool OnRequest(net::WeakPeer peer,
                           std::unique_ptr<protocol::Message> message) override {
      return manager_.OnSyncRequest(peer, std::move(message));
    }
    virtual void OnError(net::WeakPeer peer, std::string_view error) override {
      manager_.OnSyncError(peer, error);
    }

   private:
    SyncManager& manager_;
  } block_sync_handler_ = *this;

 private:
  net::SharedPeer GetSync() const {
    return sync_.lock();
  }
  bool IsSyncPeer(const protocol::Message& message) const {
    const auto sync = GetSync();
    return sync && (sync == GetPeer(message));
  }
  template <typename T, typename... Args>
  void Send(Args&&... args) {
    if (const auto sync = sync_.lock())
      broadcaster_->SendMessage<T>(sync, std::forward<Args>(args)...);
  }
  template <typename T>
  void Send(T&& msg) {
    if (const auto sync = sync_.lock())
      broadcaster_->SendMessage<T>(sync, std::make_unique<T>(std::forward<T>(msg)));
  }

  net::WeakPeer sync_;  // The peer used for timechain synchronization requests.
  HeaderSync header_sync_;
  BlockSync block_sync_;
  data::Timechain& timechain_;
};

}  // namespace hornet::node::sync
