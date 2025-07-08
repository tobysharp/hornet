// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/data/timechain.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/node/sync_handler.h"
#include "hornetlib/protocol/message/getdata.h"

namespace hornet::node {

class BlockSync {
 public:
  BlockSync(data::Timechain& timechain, SyncHandler& handler)
      : timechain_(timechain), handler_(handler) {}

  void StartSync(net::WeakPeer peer);

 protected:
  data::Timechain& timechain_;
  SyncHandler& handler_;
};

inline void BlockSync::StartSync(net::WeakPeer peer) {
  const protocol::Hash& hash = timechain_.Headers().HeaviestChain().GetHash(0);
  protocol::message::GetData getdata;
  getdata.AddInventory(protocol::Inventory::WitnessBlock(hash));
  handler_.OnRequest(peer, std::make_unique<protocol::message::GetData>(std::move(getdata)));
}

}  // namespace hornet::node
