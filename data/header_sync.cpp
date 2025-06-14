#include <stack>

#include "data/header_sync.h"

namespace hornet::data {

// Perform validation on the queued headers in the current thread. Works
// until all work is done or the given timeout (in ms) expires.
void HeaderSync::Validate(const util::Timeout& timeout /* = util::Timeout::Infinite() */) {
  // For each batch in the queue, or until timeout
  for (std::optional<Batch> batch; !timeout.IsExpired() && (batch = queue_.WaitPop(timeout));) {
    if (batch->empty()) continue;

    const protocol::Hash& parent_hash = batch->front().GetPreviousBlockHash();

    // If batch follows tip, or chain is empty
    if (chain_.Empty() || chain_.GetTipHash() == parent_hash) {
      // Clear the tree, otherwise it will have a missing branch after this step.
      tree_.Clear();

      // Validate headers and move to chain.
      if (!ValidateAndAppendToChain(*batch)) return Fail();  // Fail validation and disconnect peer
    } else {
      // If this is valid data it must be a potential reorg. So we do expect to find its
      // parent in our dynamic tree--that's what the tree is for. Otherwise it is either forking
      // from a long-ago parent that we aren't tracking (shouldn't happen in practice), or it's
      // invalid / erroneous data. Either way we will fail validation.
      auto it_parent = tree_.Find(parent_hash);

      // If batch doesn't follow a tree node
      if (!tree_.IsValidNode(it_parent)) {
        // It's possible that we didn't find the parent because we haven't yet populated the tree.
        // This happens when we were previously responding to linear batches appending to the chain.
        // In this case, the tree will be empty, and we may try to build it from the linear chain.
        PopulateReorgTreeFromChain();
        it_parent = tree_.Find(parent_hash);
      }

      // If batch follows a tree node now
      if (tree_.IsValidNode(it_parent)) {
        // Validate and append to the reorg tree
        const auto [valid, first_node_added] = ValidateAndAppendToReorgTree(*batch, it_parent);
        if (!valid) return Fail();

        // Compare against the PoW at the current tip.
        if (tree_.Latest().data.total_work > chain_.GetTipTotalWork()) {
          // Since this PoW is greater, truncate the chain to the common parent,
          // then copy this new branch into the linear chain. This is a reorg.
          PerformReorg(it_parent, tree_.UpToNodeIterator(first_node_added));
        }
      } else
        return Fail();  // Fail validation and disconnect peer
    }
  }
  // Prune historic branches from the tree.
  if (!timeout.IsExpired()) PruneReorgTree();
}

bool HeaderSync::ValidateAndAppendToChain(const Batch& batch) {
  return ValidateBatch(
      chain_.GetTipContext(), batch,
      [&](HeaderContext context) { chain_.Push(std::move(context.header), context.total_work); });
}

std::tuple<bool, HeaderSync::tree_iterator> HeaderSync::ValidateAndAppendToReorgTree(
    const Batch& batch, const tree_iterator parent_node_it) {
  // Validate the headers and track the PoW at their tip.
  // If valid, add the batch to the tree.
  auto first_node_added = tree_.NullParent();
  auto it_tail = parent_node_it;
  bool ok = ValidateBatch(parent_node_it->data, batch, [&](HeaderContext context) {
    it_tail = tree_.AddChild(it_tail, std::move(context));
    if (!tree_.IsValidNode(first_node_added)) first_node_added = it_tail;
  });
  return {ok, first_node_added};
}

template <typename Callback>
bool HeaderSync::ValidateBatch(const std::optional<HeaderContext>& parent,
                               const Batch& batch, Callback&& on_valid) const {
  std::optional<HeaderContext> latest = parent;
  for (const auto& header : batch) {
    auto validated = validator_.ValidateDownloadedHeader(latest, header);
    if (!validated) return false;
    latest = validated;
    on_valid(std::move(validated));
  }
}

// Performs a chain reorg. Takes the tail of reorg_tree_ as the new tip, and adjusts
// entries in chain_ to reflect that as the new heaviest chain.
void HeaderSync::PerformReorg(tree_iterator batch_parent, node_iterator batch_begin) {
  // Fast path: if the batch parent was already in the main chain, it's the common ancestor.
  if (chain_.GetHash(batch_parent->data.height) == batch_parent->hash) {
    // Truncate the chain back to the common ancestor fork point.
    chain_.TruncateLength(batch_parent->data.height + 1, batch_parent->data.total_work);
    // Now we need to refer back to the first element we just inserted in this batch.
    // Finally append the branch nodes to the heaviest chain.
    const auto range = tree_.FromOldest();
    for (auto it = batch_begin; it != range.end(); ++it)
      chain_.Push(it->data.header, it->data.total_work);
    return;
  }

  // We need to walk up the previous heaviest chain until we find the common ancestor.
  std::stack<tree_iterator> stack;
  const auto range = tree_.FromLatest();
  auto it = range.begin();
  for (; it != range.end() && !IsNodeInChain(it->parent); it = it->parent)
    stack.push(it);  // Save the nodes on this branch for later.

  // It's absolutely a bug if we didn't find the common ancestor in the chain!
  if (!tree_.IsValidNode(it))
    util::ThrowRuntimeError("Reorg tree unexpectedly disconnected from the heaviest chain.");

  // Truncate the chain back to the common ancestor fork point.
  const auto fork = it->parent;
  chain_.TruncateLength(fork->data.height + 1, fork->data.chain_work);

  // Now walk forward down the branch, adding headers into the heaviest chain.
  for (; !stack.empty(); stack.pop())
    chain_.Push(stack.top()->data.header, stack.top()->data.total_work);
}

// Returns true if the passed node is contained in the chain.
bool HeaderSync::IsNodeInChain(tree_iterator it) const {
  return tree_.IsValidNode(it) && (it->hash == chain_.GetHash(it->data.height));
}

// Copies headers freom the linear chain into the tree to support reorgs.
void HeaderSync::PopulateReorgTreeFromChain() {
  // Only if tree is empty and chain is not empty
  if (!tree_.Empty() || chain_.Empty()) return;

  // Determine the range of heights [start_height, end_height) that we will copy to the tree.
  const int start_height = std::max(0, chain_.Length() - depth_for_reorg_);

  // Start by rewinding through the chain which compute the work at each block in the chain.
  std::stack<HeaderContext> contexts;
  for (auto it = chain_.BeginFromTip(); it->height >= start_height; ++it) contexts.push(*it);

  // Then iterate forwards, copying the header info to the tree.
  for (auto it = tree_.NullParent(); !contexts.empty(); contexts.pop())
    it = tree_.AddChild(it, contexts.top());
}

// Prunes historic branches from the tree.
void HeaderSync::PruneReorgTree() {
  // The first node in the tree cannot be at a greater height than some other node in the tree,
  // by construction. Therefore we can check the first node's height to see if any pruning is
  // needed.
  if (!tree_.Empty() && IsHeightHistoric(tree_.Oldest().data.height)) {
    // There is some work to do. For now we just erase each node that has become historic.
    // Note the tree technically could become a DAG (no common root), but in practice this
    // shouldn't happen as it would imply our tree is too shallow, and it doesn't matter anyway.
    // Later, if we want to optimize this, we can store a multimap keyed on height and just
    // iterate ove the items that are now historic.
    const auto range = tree_.FromOldest();
    for (auto it = range.begin(); it != range.end();) {
      // TODO: Add logic to check that in pruning the tree we never allow it to become
      // totally disjoint from the heaviest chain.

      if (IsHeightHistoric(it->data.height)) {
        // Note, by this point the parent's node should have been erased already.

        // We need to find the entry in the hash map to erase. But we can't get this from
        // chain_, because this node may not have been committed to the heaviest chain.
        tree_.Erase(it);
      } else {
        // We're not going to erase this node, as it's still within our recent range.
        // However, note that its parent may have been removed in an earlier iteration of this
        // loop. In that case we invalidate the parent pointer now.
        if (IsHeightHistoric(it->data.height - 1)) it->parent = tree_.NullParent();
        ++it;
      }
    }
  }
}

bool HeaderSync::IsHeightHistoric(int height) const {
  return height + depth_for_reorg_ <= chain_.GetTipHeight();
}

}  // namespace hornet::data
