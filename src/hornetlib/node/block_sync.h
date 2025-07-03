// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/data/timechain.h"
#include "hornetlib/message/getdata.h"
#include "hornetlib/net/peer.h"
#include "hornetlib/node/sync_handler.h"

namespace hornet::node {

class BlockSync {
 public:
  BlockSync(data::Timechain& timechain, SyncHandler& handler)
      : timechain_(timechain), handler_(handler) {}

  void StartSync(net::PeerId id);

 protected:
  data::Timechain& timechain_;
  SyncHandler& handler_;
};

inline void BlockSync::StartSync(net::PeerId id) {
  const protocol::Hash& hash = timechain_.Headers().HeaviestChain().GetHash(0);
  message::GetData getdata;
  getdata.AddInventory(protocol::Inventory::WitnessBlock(hash));
  handler_.OnRequest(id, std::make_unique<message::GetData>(std::move(getdata)));
}

}  // namespace hornet::node
