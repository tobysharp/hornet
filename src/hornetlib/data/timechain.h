// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>

#include "hornetlib/data/header_timechain.h"
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

  HeaderTimechain& Headers() {
    return headers_;
  }
  const HeaderTimechain& Headers() const {
    return headers_;
  }

 private:
  static protocol::BlockHeader GetGenesisHeader() {
    protocol::BlockHeader genesis = {};
    genesis.SetCompactTarget(0x1d00ffff);
    genesis.SetVersion(1);
    genesis.SetTimestamp(0x495fab29);
    genesis.SetNonce(0x7c2bac1d);
    genesis.SetMerkleRoot("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_h);
    Assert(genesis.ComputeHash() == protocol::kGenesisHash);
    return genesis;
  }

  HeaderTimechain headers_;
};

}  // namespace hornet::data
