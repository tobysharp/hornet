// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <string>

#include "hornetlib/net/peer.h"
#include "hornetlib/protocol/message.h"

namespace hornet::node {

class SyncHandler {
 public:
  virtual ~SyncHandler() = default;
  virtual void OnRequest(net::PeerId peer, std::unique_ptr<const protocol::Message> message) = 0;
  virtual void OnError(net::PeerId id, std::string_view error) = 0;
  virtual void OnComplete(net::PeerId id) = 0;
};

}  // namespace hornet::node
