// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/data/header_chain.h"

#include <array>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/protocol/work.h"

namespace hornet::data {
namespace {

using protocol::BlockHeader;
using protocol::Hash;
using protocol::Work;

// Helper function to create a simple block header with the given previous hash
// and a unique nonce.
static BlockHeader MakeHeader(const Hash& prev, uint32_t nonce) {
  BlockHeader h{};
  h.SetVersion(1);
  h.SetPreviousBlockHash(prev);
  h.SetMerkleRoot(Hash{});
  h.SetTimestamp(1234567890 + nonce);
  h.SetCompactTarget(0x1d00ffff);
  h.SetNonce(nonce);
  return h;
}

TEST(HeaderChainTest, EmptyChainHasNoTip) {
  HeaderChain chain;
  EXPECT_TRUE(chain.Empty());
  EXPECT_EQ(chain.Length(), 0);
  EXPECT_EQ(chain.BeginFromTip(), chain.EndFromTip());
  EXPECT_FALSE(chain.Tip().has_value());
  // Calling GetTipHash on an empty chain triggers an assertion and aborts.
}

TEST(HeaderChainTest, PushSingleHeader) {
  HeaderChain chain;
  BlockHeader h1 = MakeHeader(Hash{}, 1);
  Work w1 = h1.GetWork();
  int tip = chain.Push(h1, w1);
  EXPECT_EQ(tip, 0);
  EXPECT_FALSE(chain.Empty());
  EXPECT_EQ(chain.Length(), 1);
  EXPECT_EQ(chain.GetTipHeight(), 0);
  EXPECT_EQ(chain.GetTipHash(), h1.ComputeHash());
  EXPECT_EQ(chain.GetTipTotalWork(), w1);
  ASSERT_TRUE(chain.Tip().has_value());
  EXPECT_EQ(chain.Tip()->GetNonce(), 1u);
}

TEST(HeaderChainTest, PushSpanAndHashes) {
  HeaderChain chain;
  // First header
  BlockHeader h1 = MakeHeader(Hash{}, 1);
  Work w1 = h1.GetWork();
  chain.Push(h1, w1);
  Hash h1_hash = chain.GetTipHash();

  // Second and third headers
  BlockHeader h2 = MakeHeader(h1_hash, 2);
  BlockHeader h3 = MakeHeader(h2.ComputeHash(), 3);
  Work total = w1 + h2.GetWork() + h3.GetWork();
  std::array<BlockHeader, 2> more{h2, h3};
  chain.Push(std::span<const BlockHeader>(more.data(), more.size()), total);

  EXPECT_EQ(chain.Length(), 3);
  EXPECT_EQ(chain.GetTipHeight(), 2);
  EXPECT_EQ(chain.GetHash(0), h1_hash);
  EXPECT_EQ(chain.GetTipHash(), h3.ComputeHash());
  ASSERT_TRUE(chain.Tip().has_value());
  EXPECT_EQ(chain.Tip()->GetNonce(), 3u);
}

TEST(HeaderChainTest, TruncateLengthWorks) {
  HeaderChain chain;
  BlockHeader h1 = MakeHeader(Hash{}, 1);
  BlockHeader h2 = MakeHeader(h1.ComputeHash(), 2);
  Work w1 = h1.GetWork();
  Work w2 = h2.GetWork();
  chain.Push(h1, w1);
  chain.Push(h2, w1 + w2);

  chain.TruncateLength(1, w1);
  EXPECT_EQ(chain.Length(), 1);
  EXPECT_EQ(chain.GetTipHeight(), 0);
  EXPECT_EQ(chain.GetTipHash(), h1.ComputeHash());
  EXPECT_EQ(chain.GetTipTotalWork(), w1);
}

TEST(HeaderChainTest, IterateFromTip) {
  HeaderChain chain;
  BlockHeader h1 = MakeHeader(Hash{}, 1);
  BlockHeader h2 = MakeHeader(h1.ComputeHash(), 2);
  BlockHeader h3 = MakeHeader(h2.ComputeHash(), 3);
  Work w1 = h1.GetWork();
  Work w2 = h2.GetWork();
  Work w3 = h3.GetWork();
  chain.Push(h1, w1);
  std::array<BlockHeader, 2> arr{h2, h3};
  chain.Push(std::span<const BlockHeader>(arr.data(), arr.size()), w1 + w2 + w3);

  std::vector<int> heights;
  std::vector<Hash> hashes;
  for (const auto& ctx : chain.FromTip()) {
    heights.push_back(ctx.height);
    hashes.push_back(ctx.hash);
  }
  ASSERT_EQ(heights.size(), 3u);
  EXPECT_EQ(heights[0], 2);
  EXPECT_EQ(heights[1], 1);
  EXPECT_EQ(heights[2], 0);
  EXPECT_EQ(hashes[0], h3.ComputeHash());
  EXPECT_EQ(hashes[1], h2.ComputeHash());
  EXPECT_EQ(hashes[2], h1.ComputeHash());
  auto tip_ctx = chain.GetTipContext();
  EXPECT_EQ(tip_ctx.height, 2);
  EXPECT_EQ(tip_ctx.hash, h3.ComputeHash());
}

}  // namespace
}  // namespace hornet::data
