// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include "hornetlib/net/peer.h"
#include "hornetlib/net/peer_registry.h"
#include "hornetlib/util/weak_ptr_collection.h"

namespace hornet::net {

class PeerManager {
 public:
  const PeerRegistry& GetRegistry() const {
    return registry_;
  }

  SharedPeer AddPeer(const std::string& host, uint16_t port) {
    auto peer = std::make_shared<Peer>(host, port);
    int fd = peer->GetConnection().GetSocket().GetFD();
    registry_.RegisterPeer(peer);
    peers_by_fd_[fd] = peer;
    fds_dirty_ = true;
    return peer;
  }

  void RemovePeer(std::shared_ptr<Peer> peer) {
    const int fd = peer->GetConnection().GetSocket().GetFD();
    const auto it = peers_by_fd_.find(fd);
    if (it != peers_by_fd_.end()) {
      peers_by_fd_.erase(it);
      fds_dirty_ = true;
    }

    registry_.UnregisterPeer(peer->GetId());
  }

  // Returns an iterable collection of peers that are ready to deliver input data.
  // The function will block for up to timeout_ms milliseconds, but will return
  // sooner if any one of the peers is readable or becomes readable.
  [[nodiscard]] std::vector<SharedPeer> PollRead(int timeout_ms = 0) {
    RefreshPollFDs();
    int rc = poll(poll_fds_in_.data(), poll_fds_in_.size(), timeout_ms);
    std::vector<SharedPeer> ready;
    if (rc > 0) {
      for (const auto& pfd : poll_fds_in_) {
        if (pfd.revents & POLLIN) {
          ready.push_back(peers_by_fd_[pfd.fd]);
        }
      }
    }
    return ready;
  }

  struct SelectAll {
    bool operator()(const net::Peer&) const {
      return true;
    }
  };

  // Returns an iterable collection of peers that are ready to receive output data.
  // The function will block for up to timeout_ms milliseconds, but will return
  // sooner if any one of the peers is writeable or becomes writeable.
  template <typename Select = SelectAll>
  [[nodiscard]] std::vector<SharedPeer> PollWrite(int timeout_ms = 0, Select select = Select{}) {
    RefreshPollFDs();
    std::vector<pollfd> poll_fds_out;
    for (const auto& peer : registry_.Snapshot()) {
      const Socket& socket = peer->GetConnection().GetSocket();
      if (socket.IsOpen() && select(*peer)) {
        poll_fds_out.push_back({socket.GetFD(), POLLOUT, 0});
      }
    }
    int rc = poll(poll_fds_out.data(), poll_fds_out.size(), timeout_ms);
    std::vector<SharedPeer> ready;
    if (rc > 0) {
      for (const auto& pfd : poll_fds_out) {
        if (pfd.revents & POLLOUT) {
          ready.push_back(peers_by_fd_[pfd.fd]);
        }
      }
    }
    return ready;
  }

  // Removes all the peers whose sockets have been closed.
  void RemoveClosedPeers() {
    for (const auto& peer : registry_.Snapshot()) {
      if (!peer->GetConnection().GetSocket().IsOpen()) {
        registry_.UnregisterPeer(peer->GetId());
        fds_dirty_ = true;
      }
    }
  }

 private:
  void RefreshPollFDs() {
    if (!fds_dirty_) return;
    poll_fds_in_.clear();
    peers_by_fd_.clear();
    for (const auto& peer : registry_.Snapshot()) {
      const Socket& socket = peer->GetConnection().GetSocket();
      if (socket.IsOpen()) {
        int fd = socket.GetFD();
        poll_fds_in_.push_back({fd, POLLIN, 0});
        peers_by_fd_.emplace(fd, peer);
      }
    }
    // for (const auto& [fd, _] : peers_by_fd_) {
    //   poll_fds_in_.push_back({fd, POLLIN, 0});
    //   poll_fds_out_.push_back({fd, POLLOUT, 0});
    // }
    fds_dirty_ = false;
  }

  PeerRegistry registry_;
  std::unordered_map<int, SharedPeer> peers_by_fd_;
  bool fds_dirty_ = false;
  std::vector<pollfd> poll_fds_in_;
};

}  // namespace hornet::net
