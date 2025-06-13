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

    // If batch follows tip, or store is empty
    if (store_.Empty() || store_.GetTipHash() == parent_hash) {
      // Clear the tree, otherwise it will have a missing branch after this step.
      ClearReorgTree();

      // Validate headers and move to store.
      if (!ValidateAndAppendToChain(*batch)) return Fail();  // Fail validation and disconnect peer
    } else {
      // If this is valid data it must be a potential reorg. So we do expect to find its
      // parent in our dynamic tree--that's what the tree is for. Otherwise it is either forking
      // from a long-ago parent that we aren't tracking (shouldn't happen in practice), or it's
      // invalid / erroneous data. Either way we will fail validation.
      auto it_parent = reorg_map_.find(parent_hash);

      // If batch doesn't follow a tree node
      if (it_parent == reorg_map_.end() || !IsValidNode(it_parent->second)) {
        // It's possible that we didn't find the parent because we haven't yet populated the tree.
        // This happens when we were previously responding to linear batches appending to the chain.
        // In this case, the tree will be empty, and we may try to build it from the linear store.
        PopulateReorgTreeFromChain();
        it_parent = reorg_map_.find(parent_hash);
      }

      // If batch follows a tree node now
      if (it_parent != reorg_map_.end() && IsValidNode(it_parent->second)) {
        // Validate and append to the reorg tree
        const auto [valid, first_node_added] = ValidateAndAppendToReorgTree(*batch, it_parent->second);
        if (!valid) return Fail();

        // Compare against the PoW at the current tip.
        if (reorg_tree_.back().chain_work > store_.GetTipTotalWork()) {
          // Since this PoW is greater, truncate the store to the common parent,
          // then copy this new chain into the linear store. This is a reorg.
          PerformReorg(it_parent->second, first_node_added);
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
      batch, store_.GetTipTotalWork(),
      [&](protocol::BlockHeader validated, int, const protocol::Hash&,
          const protocol::Work& total_work) { store_.Push(std::move(validated), total_work); });
}

std::tuple<bool, HeaderSync::node_iterator> HeaderSync::ValidateAndAppendToReorgTree(
    const Batch& batch, const tree_iterator parent_node_it) {
  // Validate the headers and track the PoW at their tip.
  // If valid, add the batch to the tree.
  auto first_node_added = reorg_tree_.end();
  auto it_tail = parent_node_it;
  bool ok = ValidateBatch(batch, parent_node_it->chain_work,
                          [&](protocol::BlockHeader validated, int height,
                              const protocol::Hash& hash, const protocol::Work& total_work) {
                            reorg_tree_.emplace_back(TreeNode{.parent = it_tail,
                                                              .height = height,
                                                              .chain_work = total_work,
                                                              .header = validated,
                                                              .hash = hash });
                            it_tail = reorg_tree_.rbegin();
                            if (!IsValidNode(first_node_added))
                              first_node_added = TreeToNodeIterator(it_tail);
                            reorg_map_[hash] = it_tail;
                          });
  return {ok, first_node_added};
}

void HeaderSync::ClearReorgTree() {
  reorg_tree_.clear();
  reorg_map_.clear();
}

// Performs a chain reorg. Takes the tail of reorg_tree_ as the new tip, and adjusts
// entries in store_ to reflect that as the new heaviest chain.
void HeaderSync::PerformReorg(tree_iterator batch_parent, node_iterator batch_begin) {
  // Fast path: if the batch parent was already in the main chain, it's the common ancestor.
  if (store_.GetHash(batch_parent->height) == batch_parent->hash) {
    // Truncate the chain back to the common ancestor fork point.
    store_.TruncateLength(batch_parent->height + 1, batch_parent->chain_work);
    // Now we need to refer back to the first element we just inserted in this batch.
    // Finally append the branch nodes to the heaviest chain store.
    for (auto it = batch_begin; it != reorg_tree_.end(); ++it)
      store_.Push(it->header, it->chain_work);
    return;
  }

  // We need to walk up the previous heaviest chain until we find the common ancestor.
  std::stack<tree_iterator> stack;
  auto it = reorg_tree_.rbegin();
  for (; IsValidNode(it) && !IsNodeInChainStore(it->parent); it = it->parent)
    stack.push(it);  // Save the nodes on this branch for later.

  // It's absolutely a bug if we didn't find the common ancestor in the store!
  if (!IsValidNode(it))
    util::ThrowRuntimeError("Reorg tree unexpectedly disconnected from the heaviest chain.");

  // Truncate the store back to the common ancestor fork point.
  const auto fork = it->parent;
  store_.TruncateLength(fork->height + 1, fork->chain_work);

  // Now walk forward down the branch, adding headers into the heaviest chain.
  for (; !stack.empty(); stack.pop())
    store_.Push(stack.top()->header, stack.top()->chain_work);
}

// Returns true if the passed node is contained in the chain store.
bool HeaderSync::IsNodeInChainStore(tree_iterator it) const {
  return IsValidNode(it) &&
    (it->hash == store_.GetHash(it->height));
}

// Copies headers freom the linear chain into the tree to support reorgs.
void HeaderSync::PopulateReorgTreeFromChain() {
  // If tree is empty and store is not empty
  if (reorg_tree_.empty() && !store_.Empty()) {
    // Determine the range of heights [start_height, end_height) that we will copy to the tree.
    const int start_height = std::max(0, store_.Length() - depth_for_reorg_);
    const int end_height = store_.Length();

    // Start by iterating backwards to pre-compute the work at each block in the chain, since we
    // don't store this currently in the header store (but we could do later if we want).
    protocol::Work cumulative_work = store_.GetTipTotalWork();
    std::vector<protocol::Work> work_at_height(end_height - start_height);
    for (int height = end_height - 1; height >= start_height; --height) {
      work_at_height[height - start_height] = cumulative_work;
      cumulative_work -= store_[height].GetWork();
    }

    // Then iterate forwards, copying the header info to the tree, along with the work.
    auto it_tail = reorg_tree_.rend();
    for (int height = start_height; height < end_height; ++height) {
      reorg_tree_.emplace_back(TreeNode{.parent = it_tail,
                                        .height = height,
                                        .chain_work = work_at_height[height - start_height],
                                        .header = store_[height],
                                        .hash = store_.GetHash(height) });
      it_tail = reorg_tree_.rbegin();
      reorg_map_[it_tail->hash] = it_tail;
    }
  }
}

// Prunes historic branches from the tree.
void HeaderSync::PruneReorgTree() {
  // The first node in the tree cannot be at a greater height than some other node in the tree,
  // by construction. Therefore we can check the first node's height to see if any pruning is
  // needed.
  if (!reorg_tree_.empty() && IsHeightHistoric(reorg_tree_.front().height)) {
    // There is some work to do. For now we just erase each node that has become historic.
    // Note the tree technically could become a DAG (no common root), but in practice this
    // shouldn't happen as it would imply our tree is too shallow, and it doesn't matter anyway.
    // Later, if we want to optimize this, we can store a multimap keyed on height and just
    // iterate ove the items that are now historic.
    for (auto it = reorg_tree_.begin(); it != reorg_tree_.end();) {
      // TODO: Add logic to check that in pruning the tree we never allow it to become
      // totally disjoint from the heaviest chain.

      if (IsHeightHistoric(it->height)) {
        // Note, by this point the parent's node should have been erased already.

        // We need to find the entry in the hash map to erase. But we can't get this from
        // store_, because this node may not have been committed to the heaviest chain.
        reorg_map_.erase(it->hash);
        it = reorg_tree_.erase(it);
      } else {
        // We're not going to erase this node, as it's still within our recent range.
        // However, note that its parent may have been removed in an earlier iteration of this
        // loop. In that case we invalidate the parent pointer now.
        if (IsHeightHistoric(it->height - 1)) 
          it->parent = reorg_tree_.rend();
        ++it;
      }
    }
  }
}

bool HeaderSync::IsHeightHistoric(int height) const {
  return height + depth_for_reorg_ <= store_.GetTipHeight();
}

}  // namespace hornet::data
