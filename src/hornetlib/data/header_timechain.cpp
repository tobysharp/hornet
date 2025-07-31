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
  if (height == ChainLength() - 1) return chain_tip_context_.hash;
  return ChainElement(height + 1).GetPreviousBlockHash();
}

HeaderTimechain::AddResult HeaderTimechain::Add(const HeaderContext& context) {
  if (Empty()) {
    // Genesis header
    return Add({}, context);
  }

  // Search for the parent among all tips.
  const protocol::Hash parent_hash = context.data.GetPreviousBlockHash();
  ContextIterator parent = FindTipOrForks(parent_hash);
  if (!parent) return {parent, {}};  // If no parent was found, this is a failure.
  return Add(parent, context);
}

HeaderTimechain::AddResult HeaderTimechain::Add(ConstIterator parent, const HeaderContext& context) {
  // Validate the parent we were given
  if ((parent && parent->hash != context.data.GetPreviousBlockHash()) || (!parent && !Empty()))
    util::ThrowInvalidArgument("HeaderTimechain::Add mismatched hash at height ",
                               context.height - 1, ".");
                               
  // Add the new header context to the ChainTree.
  Base::Iterator base_child_it = Base::Add(parent, context);
  Iterator context_child_it = MakeContextIterator({base_child_it, context});
  AddResult result = { context_child_it, {} };

  // Compare against the PoW at the current tip.
  if (context.total_work > chain_tip_context_.total_work) {
    // Since this PoW is greater, truncate the chain to the common parent,
    // then copy this new branch into the linear chain. This is a reorg.
    result = PromoteBranch(base_child_it);
  }

  // Prune stale tree entries before returning.
  PruneForest();

  // TODO: Ensure we didn't just prune out the thing we're about to return!
  
  return result;
}

HeaderTimechain::ConstIterator HeaderTimechain::Search(const protocol::Hash& hash) const {
  // Check chain tip and forest.
  ConstIterator lookup = FindTipOrForks(hash);
  if (lookup) return lookup;

  // Scan through chain, up to max_search_depth_ elements.
  const int min_height = std::max(0, ChainLength() - max_search_depth_);
  for (auto it = ChainTip(); it && it->height >= min_height; ++it) {
    if (it->hash == hash) return it;
  }
  return MakeContextIterator({{*this}, std::nullopt});
}

HeaderTimechain::Iterator HeaderTimechain::Search(const protocol::Hash& hash) {
  // Check chain tip and forest.
  Iterator lookup = FindTipOrForks(hash);
  if (lookup) return lookup;

  // Scan through chain, up to max_search_depth_ elements.
  const int min_height = std::max(0, ChainLength() - max_search_depth_);
  for (auto it = ChainTip(); it && it->height >= min_height; ++it) {
    if (it->hash == hash) return it;
  }
  return MakeContextIterator({{*this}, std::nullopt});
}

// Ensure both height and hash match before returning a valid Locator.
std::optional<Locator> HeaderTimechain::MakeLocator(int height, const protocol::Hash& hash) const {
  if (height < 0) util::ThrowInvalidArgument("HeaderTimechain::MakeLocator negative height.");
  if (height < ChainLength() && hash == GetChainHash(height)) 
    return height;
  const auto it = forest_.Find(hash);
  if (forest_.IsValidNode(it) && it->hash == hash)
    return hash;
  return std::nullopt;
}

HeaderTimechain::AddResult HeaderTimechain::PromoteBranch(BaseIterator tip) {
  auto [it, moved] = Base::PromoteBranch(tip, GetPolicy());
  return {MakeContextIterator({it, *ChainTip()}), moved};
}

// Prunes historic branches from the tree.
void HeaderTimechain::PruneForest() {
  Base::PruneForest(max_keep_depth_);
}

std::unique_ptr<HeaderTimechain::ValidationView> HeaderTimechain::GetValidationView(
    ConstIterator tip) const {
  return std::make_unique<HeaderTimechain::ValidationView>(*this, tip);
}

HeaderTimechain::ConstIterator HeaderTimechain::MakeContextIterator(ConstFindResult find) const {
  return {find.first, find.second ? *find.second : HeaderContext{}, GetPolicy()};
}

HeaderTimechain::Iterator HeaderTimechain::MakeContextIterator(FindResult find) {
  return {find.first, find.second ? *find.second : HeaderContext{}, GetPolicy()};
}

HeaderTimechain::ConstIterator HeaderTimechain::ChainTip() const {
  return MakeContextIterator(Base::ChainTip());
}

HeaderTimechain::Iterator HeaderTimechain::ChainTip() {
  return MakeContextIterator(Base::ChainTip());
}

HeaderTimechain::ConstIterator HeaderTimechain::FindTipOrForks(
    const protocol::Hash& hash) const {
  return MakeContextIterator(Base::FindTipOrForks(hash));
}

HeaderTimechain::Iterator HeaderTimechain::FindTipOrForks(
    const protocol::Hash& hash) {
  return MakeContextIterator(Base::FindTipOrForks(hash));
}

////////////////////////////////////////////////////////////////////////////////////////////

int HeaderTimechain::ValidationView::Length() const {
  return tip_->height + 1;
}

uint32_t HeaderTimechain::ValidationView::TimestampAt(int height) const {
  return timechain_.GetAncestorAtHeight(tip_, height).GetTimestamp();
}

std::vector<uint32_t> HeaderTimechain::ValidationView::LastNTimestamps(int count) const {
  std::vector<uint32_t> result;
  result.reserve(count);

  const int stop_height = std::max(tip_->height - count, -1);
  for (const auto& header : timechain_.AncestorsToHeight(tip_, stop_height))
    result.push_back(header.GetTimestamp());

  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace hornet::data
