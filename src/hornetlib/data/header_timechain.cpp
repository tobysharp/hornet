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

int HeaderTimechain::PushToChain(const HeaderContext& context) {
  chain_.push_back(context.header);
  chain_tip_context_ = context;
  return GetChainTipHeight();
}

const protocol::Hash& HeaderTimechain::GetChainHash(int height) const {
  Assert(height < GetChainLength());
  if (height == GetChainTipHeight()) return chain_tip_context_.hash;
  return chain_[height + 1].GetPreviousBlockHash();
}

HeaderTimechain::ParentIterator HeaderTimechain::Add(const HeaderContext& context) {
  if (chain_.empty()) {
    // Genesis header
    return BeginChain(PushToChain(context));
  }

  const protocol::Hash parent_hash = context.header.GetPreviousBlockHash();

  // If we weren't given a parent hint then first check whether we can match the chain.
  if (parent_hash == GetChainHash(context.height - 1)) return BeginChain(context.height - 1);
  // Otherwise check if we can find in the tree map.

  ParentIterator parent = BeginTree(tree_.Find(parent_hash));
  // If still no parent found, then this is a failure.
  if (!parent) return parent;

  return Add(context, parent);
}

// We specify that if a parent hint is passed, then it must be valid and must match
// the required hash, otherwise nothing is added.
HeaderTimechain::ParentIterator HeaderTimechain::Add(const HeaderContext& context,
                                                     ParentIterator parent) {
  ParentIterator result;
  const protocol::Hash& parent_hash = context.header.GetPreviousBlockHash();

  // Validate the input arguments carefully.

  // If the parent is invalid, the chain must be empty.
  bool fail = !parent.IsValid() && !chain_.empty();
  // We can only add to one parent location.
  fail |= parent.InChain() && parent.InTree();
  // If parent chain height given it must match context and chain hash.
  fail |= parent.InChain() && ((parent.ChainHeight() != context.height - 1) ||
                               (parent.ChainHeight() >= GetChainLength()) ||
                               (parent_hash != GetChainHash(parent.ChainHeight())));
  // Validate parent in tree.
  fail |= parent.InTree() && parent_hash != parent.Node()->hash;
  if (fail) util::ThrowInvalidArgument("The parent wasn't found or didn't match the requirements.");

  // Now a parent is found in the chain or in the tree.
  if (parent.ChainHeight() == GetChainTipHeight())
    result = BeginChain(PushToChain(context));
  else {
    result = BeginTree(AddChild(parent.Node(), context));
  }

  // Compare against the PoW at the current tip.
  if (result.InTree() && result.Node()->data.TotalWork() > chain_tip_context_.total_work) {
    // Since this PoW is greater, truncate the chain to the common parent,
    // then copy this new branch into the linear chain. This is a reorg.
    ReorgBranchToChain(result.Node());
    result = BeginChain(GetChainTipHeight());
  }

  // Prune stale tree entries before returning.
  PruneReorgTree();
  return result;
}

HeaderTimechain::FindResult HeaderTimechain::Find(const protocol::Hash& hash) {
  if (!chain_.empty() && chain_tip_context_.hash == hash)
    return {BeginChain(GetChainTipHeight()), chain_tip_context_};

  const TreeIterator node = tree_.Find(hash);
  if (IsValidNode(node)) return {BeginTree(node), node->data.context};

  const int min_height = std::max(0, GetChainLength() - max_search_depth_);
  HeaderContext context = chain_tip_context_;
  for (int height = GetChainTipHeight() - 1; height >= min_height; --height) {
    context = context.Rewind(chain_[height]);
    if (GetChainHash(height) == hash) return {BeginChain(height), context};
  }
  return {{*this}, std::nullopt};
}

const protocol::BlockHeader* HeaderTimechain::Find(int height, const protocol::Hash& hash) const {
  if (height < GetChainLength() && GetChainHash(height) == hash)
    return &chain_[height];

  const HeaderTree::ConstIterator node = tree_.Find(hash);
  if (tree_.IsValidNode(node) && node->data.Height() == height) 
    return &node->data.context.header;
  
  return nullptr;
}

HeaderTimechain::FindResult HeaderTimechain::HeaviestTip() const {
  if (chain_.empty()) return {{*this}, std::nullopt};
  return {BeginChain(GetChainTipHeight()), chain_tip_context_};
}

// Performs a chain reorg. Takes the tip node of a branch, and adjusts
// entries in chain_ to reflect that as the new heaviest chain.
void HeaderTimechain::ReorgBranchToChain(TreeNode* tip) {
  Assert(tip != nullptr);
  Assert(tree_.IsLeaf(tip));

  // Locate the branch root in the tree.
  std::stack<TreeNode*> stack;
  for (TreeNode& node : tree_.UpFromNode(tip)) stack.push(&node);
  const auto root = stack.top();

  // It's absolutely a bug if we didn't find a join between the tree and the chain.
  Assert(root->data.Header().GetPreviousBlockHash() == GetChainHash(root->data.Height() - 1));

  // Find the fork point in the chain.
  const HeaderContext fork = root->data.context.Rewind(chain_[root->data.Height() - 1]);

  // Copy the old chain elements into the tree.
  {
    HeaderContext context = fork;
    TreeNode* parent = nullptr;
    for (int height = root->data.Height(); height < GetChainLength(); ++height) {
      context = context.Extend(chain_[height], GetChainHash(height));
      parent = AddChild(parent, context);
    }
  }

  // Truncate the chain back to the common ancestor fork point.
  chain_.resize(root->data.Height());
  chain_tip_context_ = fork;

  // Now walk forward down the new branch, moving headers into the heaviest chain.
  for (; !stack.empty(); stack.pop())
    PushToChain(stack.top()->data.context);

  // Finally delete the chain containing the new tip from the forest.
  tree_.EraseChain(tip);
  
  // TODO: Update min_root_height_;
}

// Prunes historic branches from the tree.
void HeaderTimechain::PruneReorgTree() {
  const int min_keep_height = GetChainTipHeight() - max_keep_depth_;
  if (tree_.Empty() || min_root_height_ >= min_keep_height) return;

  min_root_height_ = std::numeric_limits<int>::max();
  const auto range = tree_.ForwardFromOldest();
  for (auto it = range.begin(); it != range.end();) {
    if ((it->data.root_height) < min_keep_height)
      it = tree_.Erase(it);
    else {
      min_root_height_ = std::min<int>(min_root_height_, it->data.root_height);
      ++it;
    }
  }
}

HeaderTimechain::TreeNode* HeaderTimechain::AddChild(TreeNode* parent, const HeaderContext& context) {
  const int root_height = parent != nullptr ? parent->data.root_height : context.height;
  min_root_height_ = std::min(min_root_height_, root_height);
  const auto it = tree_.AddChild(parent, {context, root_height});
  return tree_.IsValidNode(it) ? &*it : nullptr;
}

std::unique_ptr<HeaderTimechain::ValidationView> HeaderTimechain::GetValidationView(
    const ParentIterator& tip) const {
  return std::make_unique<HeaderTimechain::ValidationView>(*this, tip);
}

const protocol::BlockHeader& HeaderTimechain::GetAncestorAtHeight(ParentIterator tip,
                                                                  int height) const {
  if (tip.InChain()) return chain_[height];

  if (tip.Node()->data.root_height > height)
    return chain_[height];
  else {
    for (const auto& node : tree_.UpFromNode(tip.Node()))
      if (node.data.Height() == height) return node.data.Header();
  }
  util::ThrowRuntimeError("Couldn't find an ancestor at height ", height);
}

HeaderTimechain::ParentIterator HeaderTimechain::NullIterator() const {
  return {*this};
}

HeaderTimechain::ParentIterator HeaderTimechain::BeginChain(int height) const {
  return {*this, std::max(-1, height)};
}

HeaderTimechain::ParentIterator HeaderTimechain::BeginTree(TreeIterator node) const {
  return {*this, tree_.IsValidNode(node) ? &*node : nullptr};
}

HeaderTimechain::ParentIterator HeaderTimechain::BeginTree(TreeNode* node) const {
  return {*this, node};
}

auto HeaderTimechain::AncestorsToHeight(ParentIterator start, int end_height) const {
  static_assert(std::forward_iterator<ParentIterator>);
  static_assert(std::sentinel_for<int, ParentIterator>);
  return std::ranges::subrange{start, BeginChain(end_height)};
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
