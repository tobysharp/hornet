// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include <ranges>
#include <stack>

#include "hornetlib/data/header_context.h"
#include "hornetlib/data/header_timechain.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/throw.h"

namespace hornet::data {

const protocol::Hash& HeaderTimechain::GetChainHash(int height) const {
  Assert(height >= 0 && height < ChainLength());
  if (height == ChainTipHeight()) return chain_tip_context_.hash;
  return chain_[height + 1].GetPreviousBlockHash();
}

HeaderTimechain::FindResult HeaderTimechain::Add(const HeaderContext& context) {
  if (chain_.empty()) {
    // Genesis header
    return {BeginChain(PushToChain(context)), context};
  }

  // Search for the parent among all tips.
  const protocol::Hash parent_hash = context.data.GetPreviousBlockHash();
  FindResult parent = FindInTipOrForest(parent_hash);
  if (!parent.first) return {parent.first, {}};  // If no parent was found, this is a failure.
  return Add(parent, context);
}

HeaderTimechain::FindResult HeaderTimechain::Add(FindResult parent, const HeaderContext& context) {
  // Validate the parent we found
  if (parent.second->hash != context.data.GetPreviousBlockHash())
    util::ThrowInvalidArgument("HeaderTimechain::Add mismatched hash at height ", context.height - 1, ".");
  // Add the new header context to the ChainTree.
  Iterator result = Base::Add(parent.first, context);

  // Compare against the PoW at the current tip.
  if (context.total_work > chain_tip_context_.total_work) {
    // Since this PoW is greater, truncate the chain to the common parent,
    // then copy this new branch into the linear chain. This is a reorg.
    result = PromoteBranch(result);
  }

  // Prune stale tree entries before returning.
  PruneForest();

  // TODO: Ensure we didn't just prune out the thing we're about to return!
  return {result, context};
}

HeaderTimechain::FindResult HeaderTimechain::Find(const protocol::Hash& hash) {
  // Check chain tip and forest.
  FindResult lookup = FindInTipOrForest(hash);
  if (lookup.first) return lookup;

  // Scan through chain, up to max_search_depth_ elements.
  const int min_height = std::max(0, ChainLength() - max_search_depth_);
  HeaderContext context = chain_tip_context_;
  for (int height = ChainTipHeight() - 1; height >= min_height; --height) {
    context = context.Rewind(chain_[height]);
    if (GetChainHash(height) == hash) return {BeginChain(height), context};
  }
  return {{*this}, std::nullopt};
}

const protocol::BlockHeader* HeaderTimechain::Find(int height, const protocol::Hash& hash) const {
  if (height < ChainLength() && GetChainHash(height) == hash)
    return &chain_[height];

  const Forest::ConstIterator node = forest_.Find(hash);
  if (forest_.IsValidNode(node) && node->data.Height() == height) 
    return &node->data.context.data;
  
  return nullptr;
}

HeaderTimechain::Iterator HeaderTimechain::PromoteBranch(Iterator tip) {
  return Base::PromoteBranch(tip, HeaderContextPolicy{*this});
}

// Prunes historic branches from the tree.
void HeaderTimechain::PruneForest() {
  Base::PruneForest(max_keep_depth_);
}

std::unique_ptr<HeaderTimechain::ValidationView> HeaderTimechain::GetValidationView(
    const Iterator& tip) const {
  return std::make_unique<HeaderTimechain::ValidationView>(*this, tip);
}

////////////////////////////////////////////////////////////////////////////////////////////

int HeaderTimechain::ValidationView::Length() const {
  return tip_.GetHeight() + 1;
}

uint32_t HeaderTimechain::ValidationView::TimestampAt(int height) const {
  return timechain_.GetAncestorAtHeight(tip_, height).GetTimestamp();
}

std::vector<uint32_t> HeaderTimechain::ValidationView::LastNTimestamps(int count) const {
  std::vector<uint32_t> result;
  result.reserve(count);

  const int final_height = std::max(Length() - count, -1);
  for (const auto& header : timechain_.AncestorsToHeight(tip_, final_height))
    result.push_back(header.GetTimestamp());

  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace hornet::data
