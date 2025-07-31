// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "hornetnodelib/net/peer.h"

namespace hornet::node::net {

class PeerManager;

class PeerRegistry final {
  friend class PeerManager;

 public:
  SharedPeer FromId(PeerId id) const {
    std::scoped_lock lock(mutex_);
    const auto it = map_.find(id);
    return it == map_.end() ? nullptr : it->second;
  }

  std::vector<SharedPeer> Snapshot() const {
    std::scoped_lock lock(mutex_);
    std::vector<SharedPeer> vector;
    for (auto pair : map_) vector.push_back(pair.second);
    return vector;
  }

 private:
  PeerId RegisterPeer(SharedPeer peer) {
    std::scoped_lock lock(mutex_);
    const uint64_t id = next_session_++;
    map_[id] = peer;    
    peer->SetId(id);
    return id;
  }

  void UnregisterPeer(PeerId id) {
    std::scoped_lock lock(mutex_);
    if (const auto it = map_.find(id); it != map_.end())
      map_.erase(it);
  }  
  
  uint64_t next_session_ = 1;
  std::unordered_map<PeerId, SharedPeer> map_;
  mutable std::mutex mutex_;
};

}  // namespace hornet::node::net
