// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <mutex>

#include "hornetlib/data/header_timechain.h"
#include "hornetlib/data/sidecar.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/assert.h"
#include "hornetlib/util/hex.h"

namespace hornet::data {

class Timechain {
 public:
  Timechain() {
    headers_.Add(HeaderContext::Genesis(GetGenesisHeader()));
  }

  const HeaderTimechain& Headers() const {
    return headers_;
  }

  HeaderTimechain::Iterator AddHeader(HeaderTimechain::ConstIterator parent, const HeaderContext& header_context) {
    std::scoped_lock lock(mutex_);
    const auto [child_it, moved] = headers_.Add(parent, header_context);
    SidecarAddSync sync = {parent.Locator(), child_it->hash, moved};
    for (SidecarBase* sidecar : Sidecars())
      sidecar->AddSync(sync);
    return child_it;
  }

 private:
  // LockedLocator is an RAII struct that enforces thread-safe mapping from the HeaderTimechain
  // structure to any Sidecar structure.
  struct LockedLocator {
    std::unique_lock<std::mutex> lock;
    Locator locator;
    operator const Locator&() const { return locator; }
  };

  // Finds the relevant element in the HeaderTimechain, and returns an object that can be used to
  // map to the equivalent element in a Sidecar, in a thread-safe manner.
  LockedLocator Lock(int height, const protocol::Hash& hash) const {
    std::unique_lock<std::mutex> lock(mutex_);  // Lock the header timechain and sidecars.
    const std::optional<Locator> locator = headers_.MakeLocator(height, hash);
    Assert(locator.has_value());
    return {std::move(lock), *locator};
  }

  std::vector<SidecarBase*> Sidecars() {
    return {};
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

  mutable std::mutex mutex_;  // Locks header timechain and all synchronized sidecars.
  HeaderTimechain headers_;
};

}  // namespace hornet::data
