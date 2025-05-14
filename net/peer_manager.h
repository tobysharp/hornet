#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include "net/peer.h"
#include "util/weak_ptr_collection.h"

namespace hornet::net {

class PeerManager {
 public:
  using PeerList = std::list<std::weak_ptr<Peer>>;
  using PeerCollection = util::WeakPtrCollection<Peer, PeerList>;

  void AddPeer(const std::string& host, uint16_t port) {
    auto peer = std::make_shared<Peer>(host, port);
    int fd = peer->GetConnection().GetSocket().GetFD();
    peers_by_fd_.emplace(fd, std::move(peer));
    fds_dirty_ = true;
  }

  void RemovePeer(int fd) {
    peers_by_fd_.erase(fd);
    fds_dirty_ = true;
  }

  Peer& PeerByFD(int fd) {
    auto it = peers_by_fd_.find(fd);
    if (it == peers_by_fd_.end()) {
      throw std::out_of_range("PeerByFD was given a non-existent fd.");
    }
    return *it->second;
  }

  // Returns an iterable collection of peers that are ready to deliver input data.
  // The function will block for up to timeout_ms milliseconds, but will return
  // sooner if any one of the peers is readable or becomes readable.
  PeerCollection PollRead(int timeout_ms = 0) {
    RefreshPollFDs();
    int rc = poll(poll_fds_in_.data(), poll_fds_in_.size(), timeout_ms);
    PeerList ready;
    if (rc > 0) {
      for (const auto& pfd : poll_fds_in_) {
        if (pfd.revents & POLLIN) {
          ready.push_back(peers_by_fd_[pfd.fd]);
        }
      }
    }
    return ready;
  }

  // Returns an iterable collection of peers that are ready to receive output data.
  // The function will block for up to timeout_ms milliseconds, but will return
  // sooner if any one of the peers is writeable or becomes writeable.
  PeerCollection PollWrite(int timeout_ms = 0) {
    RefreshPollFDs();
    int rc = poll(poll_fds_out_.data(), poll_fds_out_.size(), timeout_ms);
    PeerList ready;
    if (rc > 0) {
      for (const auto& pfd : poll_fds_out_) {
        if (pfd.revents & POLLOUT) {
          ready.push_back(peers_by_fd_[pfd.fd]);
        }
      }
    }
    return ready;
  }

  // Removes all the peers whose sockets have been closed.
  void RemoveClosedPeers() {
    std::vector<int> to_remove;
    for (const auto& [fd, peer] : peers_by_fd_) {
      if (!peer->GetConnection().GetSocket().IsOpen()) {
        to_remove.push_back(fd);
      }
    }
    for (int fd : to_remove) {
      RemovePeer(fd);
    }
  }

 private:
  void RefreshPollFDs() {
    if (!fds_dirty_) return;
    poll_fds_in_.clear();
    poll_fds_out_.clear();
    for (const auto& [fd, _] : peers_by_fd_) {
      poll_fds_in_.push_back({fd, POLLIN, 0});
      poll_fds_out_.push_back({fd, POLLOUT, 0});
    }
    fds_dirty_ = false;
  }

  std::unordered_map<int, std::shared_ptr<Peer>> peers_by_fd_;
  bool fds_dirty_ = false;
  std::vector<pollfd> poll_fds_in_;
  std::vector<pollfd> poll_fds_out_;
};

}  // namespace hornet::net
