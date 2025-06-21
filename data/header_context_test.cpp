// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "data/header_context.h"

#include "protocol/block_header.h"
#include "protocol/hash.h"

#include <gtest/gtest.h>

namespace hornet::data {
namespace {

// Helper to create a basic block header with given previous hash and timestamp.
static protocol::BlockHeader MakeHeader(const protocol::Hash& prev,
                                        uint32_t timestamp,
                                        uint32_t nonce = 0) {
  protocol::BlockHeader h;
  h.SetVersion(1);
  h.SetPreviousBlockHash(prev);
  h.SetMerkleRoot("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"_h);
  h.SetTimestamp(timestamp);
  h.SetCompactTarget(0x1d00ffff);
  h.SetNonce(nonce);
  return h;
}

TEST(HeaderContextTest, NullContextHasInvalidHeight) {
  HeaderContext ctx = HeaderContext::Null();
  EXPECT_EQ(ctx.height, -1);
  EXPECT_EQ(ctx.hash, protocol::Hash{});
  EXPECT_EQ(ctx.local_work, protocol::Work{});
  EXPECT_EQ(ctx.total_work, protocol::Work{});
  EXPECT_EQ(ctx.header.GetVersion(), 0);
}

TEST(HeaderContextTest, GenesisCreatesExpectedContext) {
  auto genesis = MakeHeader({}, 1231006505, 2083236893);
  HeaderContext ctx = HeaderContext::Genesis(genesis);

  EXPECT_EQ(ctx.height, 0);
  EXPECT_EQ(ctx.hash, genesis.ComputeHash());
  EXPECT_EQ(ctx.local_work, genesis.GetWork());
  EXPECT_EQ(ctx.total_work, genesis.GetWork());
  EXPECT_EQ(ctx.header.ComputeHash(), genesis.ComputeHash());
}

TEST(HeaderContextTest, ExtendComputesHashAndUpdatesWork) {
  auto genesis = MakeHeader({}, 1231006505, 2083236893);
  HeaderContext gctx = HeaderContext::Genesis(genesis);

  auto header1 = MakeHeader(gctx.hash, 1231006506, 1);
  HeaderContext ctx1 = gctx.Extend(header1);

  EXPECT_EQ(ctx1.height, gctx.height + 1);
  EXPECT_EQ(ctx1.hash, header1.ComputeHash());
  EXPECT_EQ(ctx1.local_work, header1.GetWork());
  EXPECT_EQ(ctx1.total_work, gctx.total_work + header1.GetWork());
  EXPECT_EQ(ctx1.header.ComputeHash(), header1.ComputeHash());

  auto header2 = MakeHeader(ctx1.hash, 1231006507, 2);
  auto h2_hash = header2.ComputeHash();
  HeaderContext ctx2 = ctx1.Extend(header2, h2_hash);

  EXPECT_EQ(ctx2.height, ctx1.height + 1);
  EXPECT_EQ(ctx2.hash, h2_hash);
  EXPECT_EQ(ctx2.local_work, header2.GetWork());
  EXPECT_EQ(ctx2.total_work, ctx1.total_work + header2.GetWork());
}

TEST(HeaderContextTest, RewindReturnsPreviousContext) {
  auto genesis = MakeHeader({}, 1231006505, 2083236893);
  HeaderContext gctx = HeaderContext::Genesis(genesis);
  auto header1 = MakeHeader(gctx.hash, 1231006506, 1);
  HeaderContext ctx1 = gctx.Extend(header1);

  HeaderContext back = ctx1.Rewind(genesis);

  EXPECT_EQ(back.height, gctx.height);
  EXPECT_EQ(back.hash, genesis.ComputeHash());
  EXPECT_EQ(back.local_work, genesis.GetWork());
  EXPECT_EQ(back.total_work, gctx.total_work);
  EXPECT_EQ(back.header.ComputeHash(), genesis.ComputeHash());
}

}  // namespace
}  // namespace hornet::data

