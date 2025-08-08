// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <mutex>

#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/lock.h"
#include "hornetlib/data/sidecar.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/hex.h"

namespace hornet::data {

class Timechain {
 public:
  template <typename T>
  class SidecarHandle {
    friend Timechain;
    std::list<std::unique_ptr<SidecarBase>>::iterator it;
  };

  Timechain() {
    headers_.Add(HeaderContext::Genesis(GetGenesisHeader()));
  }

  ReadLock<HeaderTimechain> ReadHeaders() const {
    return { mutex_, headers_ };  // Lock header values for reading.
  }

  HeaderTimechain::Iterator AddHeader(HeaderTimechain::ConstIterator parent, const HeaderContext& header_context) {
    auto lock = LockWrite();  // Lock both structure and values of headers and sidecars, and the sidecar array.
    const auto [child_it, moved] = headers_.Add(parent, header_context);
    SidecarAddSync sync = {parent.Locator(), child_it->hash, moved};
    for (const auto& sidecar : sidecars_)
      sidecar->AddSync(sync);
    return child_it;
  }

  template <typename T>
  SidecarHandle<T> AddSidecar(std::unique_ptr<SidecarBaseT<T>> sidecar) {
    auto lock = LockWrite();  // Lock the sidecar array for writing.
    sidecars_.emplace_back(std::move(sidecar));
    SidecarHandle<T> handle;
    handle.it = std::prev(sidecars_.end());
    return handle;
  }

  // Gets metadata from a sidecar in a thread-safe manner.
  template <typename T>
  std::optional<T> Get(SidecarHandle<T> sidecar, int height, const protocol::Hash& hash) const {
    auto lock = LockRead();  // Lock the sidecar array and values of sidecars for reading.
    const std::optional<Locator> locator = headers_.MakeLocator(height, hash);
    Assert(locator.has_value());
    const T* value = Downcast<T>(sidecar)->Get(locator);
    return value != nullptr ? std::optional<T>{*value} : std::nullopt;
  }

  // Sets metadata to a sidecar in a thread-safe manner.
  template <typename T> 
  void Set(SidecarHandle<T> sidecar, int height, const protocol::Hash& hash, const T& value) {
    auto lock = LockWrite();  // Lock the sidecar array and values of sidecars for writing.
    const std::optional<Locator> locator = headers_.MakeLocator(height, hash);
    Assert(locator.has_value());    
    Downcast(sidecar)->Set(*locator, value);
  }

 private:
  [[nodiscard]] std::lock_guard<std::recursive_mutex> LockRead() const {
    return std::lock_guard{mutex_};
  }

  [[nodiscard]] std::lock_guard<std::recursive_mutex> LockWrite() {
    return std::lock_guard{mutex_};
  }

  template <typename T>
  SidecarBaseT<T>* Downcast(SidecarHandle<T> sidecar) const {
    SidecarBase* base = *sidecar.it;
    SidecarBaseT<T>* typed = static_cast<SidecarBaseT<T>*>(base);
    return typed;
  }

  static protocol::BlockHeader GetGenesisHeader() {
    protocol::BlockHeader genesis = {};
    genesis.SetCompactTarget(0x1d00ffff);
    genesis.SetVersion(1);
    genesis.SetTimestamp(0x495fab29);
    genesis.SetNonce(0x7c2bac1d);
    genesis.SetMerkleRoot("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_hash);
    Assert(genesis.ComputeHash() == protocol::kGenesisHash);
    return genesis;
  }

  mutable std::recursive_mutex mutex_;
  HeaderTimechain headers_;
  std::list<std::unique_ptr<SidecarBase>> sidecars_;
};

}  // namespace hornet::data
