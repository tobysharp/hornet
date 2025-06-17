#include <ranges>
#include <stack>

#include "data/header_chain.h"
#include "data/header_context.h"
#include "data/header_timechain.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/throw.h"

namespace hornet::data {

HeaderTimechain::ParentIterator HeaderTimechain::Add(HeaderContext context) {
  const protocol::Hash parent_hash = context.header.GetPreviousBlockHash();

  // If we weren't given a parent hint then first check whether we can match the chain.
  if (parent_hash == chain_.GetHash(context.height - 1)) return BeginChain(context.height - 1);
  // Otherwise check if we can find in the tree map.

  ParentIterator parent = BeginTree(tree_.Find(parent_hash));
  // If still no parent found, then this is a failure.
  if (!parent) return parent;

  return Add(context, parent);
}

// We specify that if a parent hint is passed, then it must be valid and must match
// the required hash, otherwise nothing is added.
HeaderTimechain::ParentIterator HeaderTimechain::Add(HeaderContext context,
                                                       const ParentIterator& parent) {
  TreeNode* result_node = nullptr;
  int result_height = -1;
  const protocol::Hash parent_hash = context.header.GetPreviousBlockHash();

  // We can only add to exactly one parent location.
  if ((parent.InChain() ^ parent.InTree())
      // If parent chain height given it must match context and chain hash.
      || (parent.InChain() && ((parent.ChainHeight() != context.height - 1) ||
                               (parent.ChainHeight() >= chain_.Length()) ||
                               (parent_hash != chain_.GetHash(parent.ChainHeight()))))
      // Validate parent in tree.
      || (parent.InTree() && parent_hash != parent.Node()->hash))
    util::ThrowInvalidArgument("The parent wasn't found or didn't match the requirements.");

  // Now a parent is found in the chain or in the tree.
  if (parent.ChainHeight() == chain_.GetTipHeight())
    result_height = chain_.Push(std::move(context.header), context.total_work);
  else {
    result_node = AddChild(parent.Node(), std::move(context));
  }

  // Compare against the PoW at the current tip.
  if (result_height < 0 && result_node->data.TotalWork() > chain_.GetTipTotalWork()) {
    // Since this PoW is greater, truncate the chain to the common parent,
    // then copy this new branch into the linear chain. This is a reorg.
    ReorgBranchToChain(result_node);
    result_height = chain_.GetTipHeight();
  }

  // Prune stale tree entries before returning.
  PruneReorgTree();
  return {*this, result_node, result_height};
}

HeaderTimechain::FindResult HeaderTimechain::Find(const protocol::Hash& hash) {
  if (!chain_.Empty() && chain_.GetTipHash() == hash) 
    return { BeginChain(chain_.GetTipHeight()), chain_.GetTipContext() };

  const TreeIterator node = tree_.Find(hash);
  if (IsValidNode(node)) 
    return { BeginTree(node), node->data.context };

  const int min_height = std::max(0, chain_.Length() - max_search_depth_);
  HeaderContext context = chain_.GetTipContext();
  for (int height = chain_.GetTipHeight() - 1; height >= min_height; --height) {
    context = context.Rewind(chain_[height]);
    if (chain_.GetHash(height) == hash) 
      return { BeginChain(height), context };
  }
  return { {*this}, std::nullopt };
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
  Assert(root->data.Header().GetPreviousBlockHash() == chain_.GetHash(root->data.Height() - 1));

  // Copy the old chain elements into the tree
  const auto fork = root->data.context.Rewind(chain_[root->data.Height() - 1]);
  {
    auto context = fork;
    TreeNode* parent = nullptr;
    for (int height = root->data.Height(); height < chain_.Length(); ++height) {
      context = context.Extend(chain_[height], chain_.GetHash(height));
      parent = AddChild(parent, context);
    }
  }

  // Truncate the chain back to the common ancestor fork point.
  chain_.TruncateLength(root->data.Height(), fork.total_work);
  
  // Now walk forward down the new branch, moving headers into the heaviest chain.
  for (; !stack.empty(); stack.pop())
    chain_.Push(std::move(stack.top()->data.Header()), stack.top()->data.TotalWork());

  // Finally delete the chain containing the new tip from the forest.
  tree_.EraseChain(tip);
}

// Prunes historic branches from the tree.
void HeaderTimechain::PruneReorgTree() {
  const int min_keep_height = chain_.GetTipHeight() - max_keep_depth_;
  if (tree_.Empty() || min_root_height_ >= min_keep_height) 
    return;

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

HeaderTimechain::TreeNode* HeaderTimechain::AddChild(TreeNode* parent, HeaderContext context) {
  const int root_height = parent != nullptr ? parent->data.root_height : context.height;
  const auto it = tree_.AddChild(parent, {std::move(context), root_height});
  return tree_.IsValidNode(it) ? &*it : nullptr;
}

std::unique_ptr<HeaderTimechain::ValidationView> HeaderTimechain::GetValidationView(
    const ParentIterator& tip) const {
  return std::make_unique<HeaderTimechain::ValidationView>(*this, tip);
}

const protocol::BlockHeader& HeaderTimechain::GetAncestorAtHeight(
    const ParentIterator& tip, int height) const {
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
  return {*this, height};
}

HeaderTimechain::ParentIterator HeaderTimechain::BeginTree(TreeIterator node) const {
  return {*this, tree_.IsValidNode(node) ? &*node : nullptr};
}

auto HeaderTimechain::AncestorsToHeight(
    const ParentIterator& start, int end_height) const {
      static_assert(std::forward_iterator<ParentIterator>);
      static_assert(std::sentinel_for<int, ParentIterator>);
      return std::ranges::subrange{start, BeginChain(end_height)};
}

////////////////////////////////////////////////////////////////////////////////////////////

std::optional<uint32_t> HeaderTimechain::ValidationView::TimestampAt(int height) const {
  return timechain_.GetAncestorAtHeight(tip_, height).GetTimestamp();
}

std::vector<uint32_t> HeaderTimechain::ValidationView::LastNTimestamps(int count) const {
  std::vector<uint32_t> result;
  result.reserve(count);

  const int final_height = tip_.GetHeight() + 1 - count;
  for (const auto& header : timechain_.AncestorsToHeight(tip_, final_height))
    result.push_back(header.GetTimestamp());

  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace hornet::data
