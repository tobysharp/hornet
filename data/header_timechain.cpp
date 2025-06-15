#pragma once

#include <stack>

#include "data/header_chain.h"
#include "data/header_context.h"
#include "data/header_timechain.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/throw.h"

namespace hornet::data {

HeaderTimechain::Position HeaderTimechain::Add(HeaderContext context) {
  Position parent = NullPosition();
  const protocol::Hash parent_hash = context.header.GetPreviousBlockHash();

  // If we weren't given a parent hint then first check whether we can match the chain.
  if (parent_hash == chain_.GetHash(context.height - 1)) parent.chain_height = context.height - 1;
  // Otherwise check if we can find in the tree map.
  else
    parent.node = tree_.Find(parent_hash);
  // If still no parent found, then this is a failure.
  if (!parent.IsValid(tree_)) return NullPosition();

  return Add(context, parent);
}

// We specify that if a parent hint is passed, then it must be valid and must match
// the required hash, otherwise nothing is added.
HeaderTimechain::Position HeaderTimechain::Add(HeaderContext context, Position parent) {
  Position result = NullPosition();
  const protocol::Hash parent_hash = context.header.GetPreviousBlockHash();

  // We can only add to exactly one parent location.
  if ((parent.InChain() ^ parent.InTree(tree_))
      // If parent chain height given it must match context and chain hash.
      || (parent.InChain() && ((parent.chain_height != context.height - 1) ||
                               (parent.chain_height >= chain_.Length()) ||
                               (parent_hash != chain_.GetHash(parent.chain_height))))
      // Validate parent in tree.
      || (parent.InTree(tree_) && parent_hash != parent.node->hash))
    util::ThrowInvalidArgument("The parent wasn't found or didn't match the requirements.");

  // Now a parent is found in the chain or in the tree.
  if (parent.chain_height == chain_.GetTipHeight())
    result.chain_height = chain_.Push(std::move(context.header), context.total_work);
  else {
    result.node = AddChild(parent.node, std::move(context));
  }

  // Compare against the PoW at the current tip.
  if (!result.InChain() && result.node->data.TotalWork() > chain_.GetTipTotalWork()) {
    // Since this PoW is greater, truncate the chain to the common parent,
    // then copy this new branch into the linear chain. This is a reorg.
    ReorgBranchToChain(result.node);
    result.chain_height = chain_.GetTipHeight();
  }

  // Prune stale tree entries before returning.
  PruneReorgTree();
  return result;
}

// Performs a chain reorg. Takes the tip node of a branch, and adjusts
// entries in chain_ to reflect that as the new heaviest chain.
void HeaderTimechain::ReorgBranchToChain(tree_iterator tip) {
  if (!tree_.IsValidNode(tip)) util::ThrowInvalidArgument("Invalid tip for reorg.");

  // Locate the branch root in the tree.
  std::stack<tree_iterator> stack;
  auto range = tree_.FromNode(tip);
  for (auto it = range.begin(); it != range.end(); ++it) stack.push(it);
  const auto root = stack.top();

  // It's absolutely a bug if we didn't find a join between the tree and the chain.
  Assert(root->data.Header().GetPreviousBlockHash() == chain_.GetHash(root->data.Height() - 1));

  // Copy the old chain elements into the tree
  const auto fork = RewindRoot(root);
  auto context = fork;
  auto parent = tree_.NullParent();
  for (int height = root->data.Height(); height < chain_.Length(); ++height) {
    context = context.Extend(chain_[height]);
    parent = AddChild(parent, context);
  }

  // Truncate the chain back to the common ancestor fork point.
  chain_.TruncateLength(root->data.Height(), fork.total_work);

  // Now walk forward down the branch, moving headers into the heaviest chain.
  for (; !stack.empty(); stack.pop()) {
    chain_.Push(std::move(stack.top()->data.Header()), stack.top()->data.TotalWork());
    tree_.Erase(stack.top());
  }
}

// Prunes historic branches from the tree.
void HeaderTimechain::PruneReorgTree() {
  if (tree_.Empty() || min_root_height_ >= GetMinKeepHeight()) 
    return;

  min_root_height_ = std::numeric_limits<int>::max();
  const int min_keep_height = GetMinKeepHeight();

  const auto range = tree_.FromLatest();
  for (auto it = range.begin(); it != range.end();) {
    if ((it->data.root_height) < min_keep_height)
      it = tree_.Erase(it);
    else {
      min_root_height_ = std::min<int>(min_root_height_, it->data.root_height);
      ++it;
    }
  }
}

HeaderContext HeaderTimechain::RewindRoot(tree_iterator root) const {
  if (!IsValidNode(root) || IsValidNode(root->parent)) util::ThrowInvalidArgument("Invalid root.");

  return root->data.context.Rewind(chain_[root->data.Height() - 1],
                                   chain_.GetHash(root->data.Height() - 1));
}

HeaderTimechain::tree_iterator HeaderTimechain::AddChild(tree_iterator parent,
                                                         HeaderContext context) {
  const int root_height = IsValidNode(parent) ? parent->data.root_height : context.height;
  return tree_.AddChild(parent, {std::move(context), root_height});
}

}  // namespace hornet::data
