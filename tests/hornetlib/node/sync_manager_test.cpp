// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/node/sync_manager.h"

#include "hornetlib/data/timechain.h"
#include "hornetlib/net/bitcoind.h"
#include "hornetlib/node/protocol_loop.h"
#include "hornetlib/node/peer_negotiator.h"
#include "hornetlib/node/sync_manager.h"
#include "hornetlib/protocol/handshake.h"
#include "hornetlib/util/log.h"
#include "hornetlib/util/timeout.h"

#include <gtest/gtest.h>

namespace hornet::node {
namespace {

TEST(SyncManagerTest, TestGetHeaders) {
    net::Bitcoind node = net::Bitcoind::ConnectOrLaunch();
    net::PeerManager peers;
    ProtocolLoop loop(peers);
    PeerNegotiator negotiator;
    loop.AddEventHandler(&negotiator);
    const auto peer = loop.AddOutboundPeer(net::kLocalhost, node.GetPort());
    util::Timeout timeout(1000);  // Wait up to one second for the handshake to complete.
    loop.RunMessageLoop([&](const ProtocolLoop&) {
        return timeout.IsExpired() || peer->GetHandshake().IsComplete();
    });
    EXPECT_TRUE(peer->GetHandshake().IsComplete());
}

TEST(SyncManagerTest, TestMainnetSyncHeaders) {
    net::Bitcoind node = net::Bitcoind::Connect();
    net::PeerManager peers;
    ProtocolLoop loop(peers);
    PeerNegotiator negotiator;
    loop.AddEventHandler(&negotiator);
    data::Timechain timechain;
    SyncManager sync(timechain);
    loop.AddEventHandler(&sync);

    loop.AddOutboundPeer(net::kLocalhost, node.GetPort());
    loop.RunMessageLoop([&](const ProtocolLoop&) {
        return timechain.ReadHeaders()->ChainLength() >= 9000;
    });
    LogDebug() << "Header count: " << timechain.ReadHeaders()->ChainLength();
    EXPECT_TRUE(timechain.ReadHeaders()->ChainLength() >= 9000);
}

class NoHeadersSyncManager : public SyncManager {
 public:
  NoHeadersSyncManager(data::Timechain& timechain) : SyncManager(timechain) {}
  bool IsDone() const { return done_; }

  virtual void OnMessage(const protocol::message::Headers& headers) override {
    protocol::message::Headers v2;
    v2.SetEnvelope(*headers.GetEnvelope());
    v2.AddBlockHeader(headers.GetBlockHeaders()[0]);
    SyncManager::OnMessage(v2);
  }

 protected:
  bool done_ = true;
};

TEST(SyncManagerTest, TestMainnetSyncBlocks) {
    net::Bitcoind node = net::Bitcoind::Connect();
    net::PeerManager peers;
    ProtocolLoop loop(peers);
    PeerNegotiator negotiator;
    loop.AddEventHandler(&negotiator);
    data::Timechain timechain;
    NoHeadersSyncManager sync(timechain);
    loop.AddEventHandler(&sync);
    loop.AddOutboundPeer(net::kLocalhost, node.GetPort());
    loop.RunMessageLoop([&](const ProtocolLoop&) {
        return sync.IsDone();
    });
}

}  // namespace
}  // namespace hornet::node