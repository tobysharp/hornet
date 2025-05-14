#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "net/peer.h"

namespace hornet::net {

class PeerManager {
 public:
  class PeerIterator {
   public:
    PeerIterator(PeerManager& manager, std::vector<int>::iterator it)
        : manager_(manager), it_(it) {}

    Peer& operator*() const {
      return manager_.PeerByFD(*it_);
    }
    PeerIterator& operator++() {
      ++it_;
      return *this;
    }
    bool operator!=(const PeerIterator& rhs) const {
      return it_ != rhs.it_;
    }

   private:
    PeerManager& manager_;
    std::vector<int>::iterator it_;
  };

  class PeerCollection {
   public:
    PeerCollection(PeerManager& manager, std::vector<int> fds)
        : manager_(manager), fds_(std::move(fds)) {}
    PeerIterator begin() {
      return {manager_, fds_.begin()};
    }
    PeerIterator end() {
      return {manager_, fds_.end()};
    }

   private:
    PeerManager& manager_;
    std::vector<int> fds_;
  };

  void AddPeer(const std::string& host, uint16_t port) {
    Peer peer(host, port);
    int fd = peer.Conn().GetSocket().GetFD();
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
    return it->second;
  }

  // Returns an iterable collection of peers that are ready to deliver input data.
  // The function will block for up to timeout_ms milliseconds, but will return
  // sooner if any one of the peers is readable or becomes readable.
  PeerCollection PollRead(int timeout_ms = 0) {
    RefreshPollFDs();
    int rc = poll(poll_fds_in_.data(), poll_fds_in_.size(), timeout_ms);
    std::vector<int> ready;
    if (rc > 0) {
      for (const auto& pfd : poll_fds_in_) {
        if (pfd.revents & POLLIN) {
          ready.push_back(pfd.fd);
        }
      }
    }
    return {*this, ready};
  }

  // Returns an iterable collection of peers that are ready to receive output data.
  // The function will block for up to timeout_ms milliseconds, but will return
  // sooner if any one of the peers is writeable or becomes writeable.
  PeerCollection PollWrite(int timeout_ms = 0) {
    RefreshPollFDs();
    int rc = poll(poll_fds_out_.data(), poll_fds_out_.size(), timeout_ms);
    std::vector<int> ready;
    if (rc > 0) {
      for (const auto& pfd : poll_fds_out_) {
        if (pfd.revents & POLLOUT) {
          ready.push_back(pfd.fd);
        }
      }
    }
    return {*this, ready};
  }

  // Removes all the peers whose sockets have been closed.
  void RemoveClosedPeers() {
    std::vector<int> to_remove;
    for (const auto& [fd, peer] : peers_by_fd_) {
      if (!peer.GetConnection().GetSocket().IsOpen()) {
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

  std::unordered_map<int, Peer> peers_by_fd_;
  bool fds_dirty_ = false;
  std::vector<pollfd> poll_fds_in_;
  std::vector<pollfd> poll_fds_out_;
};

}  // namespace hornet::net
