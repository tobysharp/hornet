// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <string>

#include "hornetlib/protocol/message.h"
#include "hornetnodelib/net/peer.h"

namespace hornet::node::sync {

class SyncHandler {
 public:
  virtual ~SyncHandler() = default;
  virtual bool OnRequest(net::WeakPeer peer, std::unique_ptr<protocol::Message> message) = 0;
  virtual void OnError(net::WeakPeer peer, std::string_view error) = 0;
  virtual void OnComplete(net::WeakPeer peer) = 0;
};

}  // namespace hornet::node::sync
