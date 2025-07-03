// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <chrono>
#include <string_view>

#include "hornetlib/data/timechain.h"
#include "hornetlib/message/getheaders.h"
#include "hornetlib/message/headers.h"
#include "hornetlib/message/verack.h"
#include "hornetlib/node/block_sync.h"
#include "hornetlib/node/broadcaster.h"
#include "hornetlib/node/header_sync.h"
#include "hornetlib/node/inbound_handler.h"
#include "hornetlib/node/sync_handler.h"
#include "hornetlib/protocol/constants.h"

namespace hornet::node {

// Class for managing initial block download
class SyncManager : public InboundHandler {
 public:
  SyncManager(data::Timechain& timechain, Broadcaster& broadcaster)
      : InboundHandler(&broadcaster),
        header_sync_(timechain.Headers(), header_sync_handler_),
        block_sync_(timechain, block_sync_handler_) {}
  SyncManager() = delete;

  void OnHandshakeCompleted(std::shared_ptr<net::Peer> peer) {
    {
      const std::shared_ptr<net::Peer> sync = sync_.lock();
      if (sync && !sync->IsDropped()) return;  // We already have a sync peer
    }
    // Adopt a new peer to use for timechain sync requests
    sync_ = peer;

    // Send a message requesting headers (example only).
    header_sync_.StartSync(sync_);
  }

  virtual void Visit(const message::Verack&) override {
    if (GetPeer()->GetHandshake().IsComplete()) OnHandshakeCompleted(GetPeer());
  }

  virtual void Visit(const message::Headers& headers) override {
    // TODO: [HOR-20: Request tracking]
    // (https://linear.app/hornet-node/issue/HOR-20/request-tracking)
    if (!IsSyncPeer()) return;

    // Pass the headers message to the HeaderSync object.
    header_sync_.OnHeaders(GetSync(), headers);
  }

  const HeaderSync& GetHeaderSync() const {
    return header_sync_;
  }

 protected:
  // Called by HeaderSync or BlockSync when a validation occurred. Drops the sync peer.
  void OnSyncError(net::PeerId id, std::string_view error) {
    LogWarn() << error;
    if (auto peer = net::Peer::FromId(id)) peer->Drop();
  }
  void OnSyncRequest(net::PeerId id, std::unique_ptr<const protocol::Message> message) {
    if (auto peer = net::Peer::FromId(id)) broadcaster_->SendMessage(peer, std::move(message));
  }
  // Called by HeaderSync when a peer's header validation is up-to-date.
  void OnHeaderSyncComplete(net::PeerId id) {
    Assert(net::Peer::IsSame(id, sync_));
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
    block_sync_.StartSync(id);
  }

  class HeaderSyncHandler final : public node::SyncHandler {
   public:
    HeaderSyncHandler(SyncManager& manager) : manager_(manager) {}
    virtual void OnComplete(net::PeerId id) override {
      manager_.OnHeaderSyncComplete(id);
    }
    virtual void OnRequest(net::PeerId id,
                           std::unique_ptr<const protocol::Message> message) override {
      manager_.OnSyncRequest(id, std::move(message));
    }
    virtual void OnError(net::PeerId id, std::string_view error) override {
      manager_.OnSyncError(id, error);
    }

   private:
    SyncManager& manager_;
  } header_sync_handler_ = *this;

  class BlockSyncHandler final : public node::SyncHandler {
   public:
    BlockSyncHandler(SyncManager& manager) : manager_(manager) {}
    virtual void OnComplete(net::PeerId) override {}
    virtual void OnRequest(net::PeerId id,
                           std::unique_ptr<const protocol::Message> message) override {
      manager_.OnSyncRequest(id, std::move(message));
    }
    virtual void OnError(net::PeerId id, std::string_view error) override {
      manager_.OnSyncError(id, error);
    }

   private:
    SyncManager& manager_;
  } block_sync_handler_ = *this;

 private:
  std::shared_ptr<net::Peer> GetSync() const {
    return sync_.lock();
  }
  bool IsSyncPeer() const {
    const auto sync = GetSync();
    return sync && (sync == GetPeer());
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

  std::weak_ptr<net::Peer> sync_;  // The peer used for timechain synchronization requests.
  HeaderSync header_sync_;
  BlockSync block_sync_;
};

}  // namespace hornet::node
