#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "message/visitor.h"
#include "net/peer.h"
#include "node/broadcaster.h"
#include "node/inbound_message.h"
#include "node/outbound_message.h"
#include "protocol/factory.h"
#include "protocol/message.h"

namespace hornet::node {

class Processor : public message::Visitor {
 public:
  Processor(const protocol::Factory& factory, Broadcaster& broadcaster);

  void InitiateHandshake(std::shared_ptr<net::Peer> peer);
  void Process(const InboundMessage& msg);

  // Message handlers
  void Visit(const message::Ping&);
  void Visit(const message::SendCompact&);
  void Visit(const message::Verack&);
  void Visit(const message::Version&);
  
 private:
  template <typename T, typename... Args>
  void Reply(Args... args) {
    SendMessage(GetPeer(), std::make_unique<T>(std::forward<Args>(args)...));
  }
  template <typename T>
  void Reply(std::unique_ptr<T>&& msg) {
    SendMessage(GetPeer(), std::move(msg));
  }
  template <typename T>
  void SendMessage(std::shared_ptr<net::Peer> peer, std::unique_ptr<T>&& message) {
    if (peer) {
      std::unique_ptr<protocol::Message> base{static_cast<protocol::Message*>(message.release())};
      OutboundMessage outbound{std::move(base)};
      broadcaster_.SendToOne(peer, std::move(outbound));
    }
  }
  std::shared_ptr<net::Peer> GetPeer() const {
    return inbound_->GetPeer();
  }
  protocol::Capabilities& GetPeerCapabilities() {
    return GetPeer()->GetCapabilities();
  }  
  void AdvanceHandshake(std::shared_ptr<net::Peer> peer,
                        protocol::Handshake::Transition transition);
  void SendPeerPreferences();

  const InboundMessage* inbound_ = nullptr;
  const protocol::Factory& factory_;
  Broadcaster& broadcaster_;
};

}  // namespace hornet::node
