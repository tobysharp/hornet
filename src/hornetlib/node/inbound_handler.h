// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "message/visitor.h"
#include "node/inbound_message.h"

namespace hornet::node {

class InboundHandler : public message::Visitor {
 public:
  InboundHandler(Broadcaster* broadcaster = nullptr) : broadcaster_(broadcaster) {}
  void Process(const InboundMessage& inbound) {
    // Use an RAII pattern to guarantee inbound_ is valid if and only if we're inside
    // one of the Visit methods.
    struct ScopedInboundGuard {
      ScopedInboundGuard(InboundHandler& p, const InboundMessage* msg) : p_(p) {
        p_.inbound_ = msg;
      }
      ~ScopedInboundGuard() {
        p_.inbound_ = nullptr;
      }
      InboundHandler& p_;
    };

    // Check for liveness of peer, i.e. that it hasn't been removed from PeerManager.
    if (const auto peer = inbound.GetPeer()) {
      // Check for the socket still being open. It may have been closed by the Connection.
      if (peer->GetConnection().GetSocket().IsOpen()) {
        ScopedInboundGuard guard{*this, &inbound};
        // Dispatch the message for processing via the Visitor pattern.
        inbound.GetMessage().Accept(*this);
      }
    }
  }

 protected:
  // Returns the peer that sent the current message.
  std::shared_ptr<net::Peer> GetPeer() const {
    return inbound_->GetPeer();
  }
  template <typename T, typename... Args>
  void Reply(Args&&... args) {
    if (broadcaster_ != nullptr)
      broadcaster_->SendMessage<T, Args...>(GetPeer(), std::forward<Args>(args)...);
  }
  template <typename T>
  void Reply(std::unique_ptr<T>&& msg) {
    if (broadcaster_ != nullptr)
      broadcaster_->SendMessage<T>(GetPeer(), std::forward(msg));
  }
  template <typename T>
  void Send(const std::shared_ptr<net::Peer>& peer, std::unique_ptr<T>&& msg) {
    if (broadcaster_ != nullptr)
      broadcaster_->SendMessage<T>(peer, std::forward<std::unique_ptr<T>>(msg));
  }
  // TODO: Need to guard inbound_ with a mutex if an instance requires multi-threaded operation.
  const InboundMessage* inbound_ = nullptr;
  Broadcaster* broadcaster_ = nullptr;
};

}  // namespace hornet::node