// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <functional>
#include <memory>

#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/inventory.h"
#include "hornetlib/protocol/message/block.h"
#include "hornetlib/protocol/message/getdata.h"
#include "hornetnodelib/net/peer.h"
#include "hornetnodelib/net/peer_manager.h"
#include "hornetnodelib/dispatch/peer_negotiator.h"
#include "hornetnodelib/dispatch/protocol_loop.h"

namespace hornet::test {

// A ProtocolSession contains the essential ingredients for a functioning p2p protocol session:
// A peer manager, a message loop, and a negotiator for handling necessary messages for the connections.
class ProtocolSession {
 public:
  ProtocolSession() :
    loop_{peers_} {
      loop_.AddEventHandler(&negotiator_);
    }

    node::dispatch::ProtocolLoop& Loop() {
      return loop_;
    }

    void RunUntilHandshake(node::net::SharedPeer peer) {
      loop_.RunMessageLoop([&] { return peer->GetHandshake().IsComplete(); });
    }

    void RunUntil(std::function<bool()> break_condition) {
      return loop_.RunMessageLoop(break_condition);
    }

    std::shared_ptr<const protocol::Block> DownloadBlock(node::net::SharedPeer peer, const protocol::Hash& hash) {
      RunUntilHandshake(peer);

      auto getdata = std::make_unique<protocol::message::GetData>();
      getdata->AddInventory(protocol::Inventory::WitnessBlock(hash));
      loop_.SendToOne(peer, std::move(getdata));
  
      std::shared_ptr<const protocol::Block> block;
      const auto handler = MakeLambdaHandler<protocol::message::Block>([&](const protocol::message::Block& message) {
        block = message.GetBlock();
      });
      loop_.AddEventHandler(handler.get());
      loop_.RunMessageLoop([&]() { return block != nullptr; });
      return block;
    }

 private:
  template <typename MessageT, typename Fn>
  class LambdaHandler final : public node::dispatch::EventHandler {
    public:
    explicit LambdaHandler(Fn&& func) : func_(std::forward<Fn>(func)) {}
    void OnMessage(const MessageT& message) override {
      func_(message);
    }
    private:
    Fn func_;
  };
  template <typename MessageT, typename Fn>
  std::unique_ptr<LambdaHandler<MessageT, Fn>> MakeLambdaHandler(Fn&& func) {
    return std::make_unique<LambdaHandler<MessageT, Fn>>(std::forward<Fn>(func));
  }

  node::net::PeerManager peers_;
  node::dispatch::ProtocolLoop loop_;
  node::dispatch::PeerNegotiator negotiator_;
};

}  // namespace hornet::test
