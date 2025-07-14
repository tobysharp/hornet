// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "hornetlib/net/peer.h"
#include "hornetlib/net/peer_registry.h"
#include "hornetlib/util/weak_ptr_collection.h"

namespace hornet::net {

class PeerManager {
 public:
  using PeerArray = std::vector<SharedPeer>;
  struct PollResult {
    PeerArray read;
    PeerArray write;
    bool empty;       // True if no peers were available to poll.
  };

  const PeerRegistry& GetRegistry() const {
    return registry_;
  }

  SharedPeer AddPeer(const std::string& host, uint16_t port) {
    auto peer = std::make_shared<Peer>(host, port);
    registry_.RegisterPeer(peer);
    return peer;
  }

  void RemovePeer(std::shared_ptr<Peer> peer) {
    registry_.UnregisterPeer(peer->GetId());
  }

  struct SelectAll {
    bool operator()(const net::Peer&) const {
      return true;
    }
  };

  template <typename Select = SelectAll>
  [[nodiscard]] PollResult PollReadWrite(int timeout_ms = 0, Select select_write = Select{}) {
    std::vector<pollfd> poll_fds;
    std::unordered_map<int, SharedPeer> fd_to_peer;

    for (const auto& peer : registry_.Snapshot()) {
      const Socket& socket = peer->GetConnection().GetSocket();
      if (socket.IsOpen()) {
        const int fd = socket.GetFD();
        short events = POLLIN;
        if (select_write(*peer)) events |= POLLOUT;
        poll_fds.push_back({fd, events, 0});
        fd_to_peer[fd] = peer;
      }
    }

    PollResult result;
    result.empty = poll_fds.empty();
    int rc = poll(poll_fds.data(), poll_fds.size(), timeout_ms);

    if (rc > 0) {
      for (const auto& pfd : poll_fds) {
        if (pfd.revents & (POLLIN | POLLOUT)) {
          const SharedPeer& peer = fd_to_peer[pfd.fd];
          if (pfd.revents & POLLIN) result.read.push_back(peer);
          if (pfd.revents & POLLOUT) result.write.push_back(peer);
        }
      }
    }
    return result;
  }

  // Removes all the peers whose sockets have been closed.
  void RemoveClosedPeers() {
    for (const auto& peer : registry_.Snapshot()) {
      if (!peer->GetConnection().GetSocket().IsOpen()) registry_.UnregisterPeer(peer->GetId());
    }
  }

 private:
  PeerRegistry registry_;
};

}  // namespace hornet::net
