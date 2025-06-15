#include <ranges>
#include <stack>

#include "data/header_chain.h"
#include "data/header_context.h"
#include "data/header_timechain.h"
#include "protocol/block_header.h"
#include "protocol/hash.h"
#include "util/throw.h"

namespace hornet::data {

HeaderTimechain::AncestorIterator HeaderTimechain::Add(HeaderContext context) {
  const protocol::Hash parent_hash = context.header.GetPreviousBlockHash();

  // If we weren't given a parent hint then first check whether we can match the chain.
  if (parent_hash == chain_.GetHash(context.height - 1)) return BeginChain(context.height - 1);
  // Otherwise check if we can find in the tree map.

  AncestorIterator parent = BeginTree(tree_.Find(parent_hash));
  // If still no parent found, then this is a failure.
  if (!parent) return parent;

  return Add(context, parent);
}

// We specify that if a parent hint is passed, then it must be valid and must match
// the required hash, otherwise nothing is added.
HeaderTimechain::AncestorIterator HeaderTimechain::Add(HeaderContext context,
                                                       const AncestorIterator& parent) {
  tree_iterator result_node = tree_.NullParent();
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

HeaderTimechain::AncestorIterator HeaderTimechain::Find(const protocol::Hash& hash) {
  if (chain_.GetTipHash() == hash) return BeginChain(chain_.GetTipHeight());

  const tree_iterator node = tree_.Find(hash);
  if (IsValidNode(node)) return BeginTree(node);

  const int min_height = std::max(0, chain_.Length() - max_search_depth_);
  for (int height = chain_.GetTipHeight() - 1; height >= min_height; --height) {
    if (chain_.GetHash(height) == hash) return BeginChain(height);
  }
  return NullPosition();
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
    context = context.Extend(chain_[height], chain_.GetHash(height));
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
  if (tree_.Empty() || min_root_height_ >= GetMinKeepHeight()) return;

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
  return root->data.context.Rewind(chain_[root->data.Height() - 1]);
}

HeaderTimechain::tree_iterator HeaderTimechain::AddChild(tree_iterator parent,
                                                         HeaderContext context) {
  const int root_height = IsValidNode(parent) ? parent->data.root_height : context.height;
  return tree_.AddChild(parent, {std::move(context), root_height});
}

std::unique_ptr<HeaderTimechain::ValidationView> HeaderTimechain::GetValidationView(
    const AncestorIterator& tip) const {
  return std::make_unique<HeaderTimechain::ValidationView>(*this, tip);
}

std::optional<protocol::BlockHeader> HeaderTimechain::GetAncestorAtHeight(
    const AncestorIterator& tip, int height) const {
  const auto range = AncestorsToHeight(tip, height);
  for (auto i = range.begin(); i != range.end(); ++i)
    if (i.InChain()) return chain_[height];
  return range.end().TryGet();
}

HeaderTimechain::AncestorIterator HeaderTimechain::NullPosition() const {
  return {*this, tree_.NullParent(), -1};
}

HeaderTimechain::AncestorIterator HeaderTimechain::BeginChain(int height) const {
  return {*this, height};
}

HeaderTimechain::AncestorIterator HeaderTimechain::BeginTree(tree_iterator node) const {
  return {*this, node};
}

std::ranges::subrange<HeaderTimechain::AncestorIterator> HeaderTimechain::AncestorsToHeight(
    const AncestorIterator& start, int end_height) const {
      return {start, BeginChain(end_height)};
}

////////////////////////////////////////////////////////////////////////////////////////////

std::optional<uint32_t> HeaderTimechain::ValidationView::TimestampAt(int height) const {
  const auto ancestor = timechain_.GetAncestorAtHeight(tip_, height);
  if (ancestor) return ancestor->GetTimestamp();
  return {};
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
