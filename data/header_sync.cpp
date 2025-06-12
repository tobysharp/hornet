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
      // TODO: Validate headers and move to store.

      // Clear the tree, otherwise it will have a missing branch after this step.
      reorg_tree_.clear();
      reorg_map_.clear();
    } else {
      // If this is valid data it must be a potential reorg. So we do expect to find its
      // parent in our dynamic tree--that's what the tree is for. Otherwise it is either forking
      // from a long-ago parent that we aren't tracking (shouldn't happen in practice), or it's
      // invalid / erroneous data. Either way we will fail validation.

      auto it_parent = reorg_map_.find(parent_hash);

      // If batch doesn't follow a tree node
      if (it_parent == reorg_map_.end() || !IsValidNode(it_parent->second)) {
        // The only time this is acceptable is when we haven't yet populated the tree.
        // For example, because up until now we've just been responding to linear headers messages,
        // and this is the first time anything different has come our way. In this case, the tree
        // of recent headers will be empty, and we may try to build it from the linear store.
        PopulateReorgTreeFromChain();
        it_parent = reorg_map_.find(parent_hash);
      }

      // If batch follows a tree node now
      if (it_parent != reorg_map_.end() && IsValidNode(it_parent->second)) {
        // Validate the headers and track the PoW at their tip.
        // If valid, add the batch to the tree.
        // Compare against the PoW at the current tip.
        // If this PoW is greater, then truncate the store to the common parent,
        // then copy this new chain into the linear store.
      } else {
        // Fail validation and disconnect peer.
      }
    }
  }

  // Prune historic branches from the tree.
  // TODO: Consider whether to skip this if timeout expired.
  PruneReorgTree();
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
    std::vector<protocol::Work> work_at_height(end_height - start_height + 1);
    for (int height = end_height - 1; height >= start_height; --height) {
      work_at_height[height - start_height] = cumulative_work;
      cumulative_work -= store_[height].GetWork();
    }

    // Then iterate forwards, copying the header info to the tree, along with the work.
    auto it_tail = reorg_tree_.end();
    for (int height = start_height; height < end_height; ++height) {
      reorg_tree_.emplace_back(TreeNode{.parent = it_tail,
                                        .height = height,
                                        .chain_work = work_at_height[height - start_height],
                                        .header = store_[height]});
      it_tail = std::prev(reorg_tree_.end());
      reorg_map_[store_.GetHash(height)] = it_tail;
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
      if (IsHeightHistoric(it->height)) {
        // Note, by this point the parent's node should have been erased already.
      
        // We need to find the entry in the hash map to erase. But we can't get this from
        // store_, because this node may not have been committed to the heaviest chain.
        // Note that we could add the current block hash to TreeNode if we want to pay for
        // the memory use rather than the compute. But unless this is a leaf node or is 
        // followed by a fork node, we will find our hash in the next node. Furthermore, we
        // should not generally be pruning leaf nodes so this should be a rare case.
        const auto next = std::next(it);
        protocol::Hash hash = (IsValidNode(next) && next->parent == it)
                                  ? next->header.GetPreviousBlockHash()
                                  : it->header.ComputeHash();
        reorg_map_.erase(reorg_map_.find(hash));
        it = reorg_tree_.erase(it);
      } else {
        // We're not going to erase this node, as it's still within our recent range.
        // However, note that its parent may have been removed in an earlier iteration of this loop.
        // In that case we invalidate the parent pointer now.
        if (IsHeightHistoric(it->height - 1)) it->parent = reorg_tree_.end();
        // TODO: Is the above line correct to set the parent to null?

        ++it;
      }
    }
  }
}

bool HeaderSync::IsHeightHistoric(int height) const {
  return height + depth_for_reorg_ <= store_.GetTipHeight();
}

/*
   if (parent_hash != store_.GetTipHash()) {
      // This batch is a linear sequence (that is the current assumption based on headers message),
      // but it doesn't connect to the existing tip. It may be a fork/reorg, or erroneous /
      // malicious. First, try to locate the parent hash in our dynamic tree structure. If it's
      // found, we'll be able to perform the reorg in a straightforward manner.
      const auto it = hash_map_.find(parent_hash);
      if (it != hash_map_.end()) {
        const TreeNode& parent_node = verified_tree_[it->second];
        const int parent_height = parent_node.height;
        // TODO: Truncate the linear store to parent_height.

        // !! Ah but wait, don't we need to check whether this would result in more or less PoW
        // before replacing the old chain with this reorg. Yes, if this batch is valid but lower
        // work than another branch, then I think we just stuff this branch in the tree and
        // continue.
      } else {
        // We were given a parent hash that we don't recognize. It could be that the hash is so far
        // back that we aren't tracking it in our tree. But this shouldn't happen on genuine data.
        // It's also possible this is erroneous data, so either way we treat this as a validation
        // error.
        // TODO: Fail validation and disconnect peer.
      }
    }

    // At this point the parent should be the tip of the linear stored array. Otherwise something
    // went wrong.
    Assert(parent_hash == store_.GetTipHash());

    // Now this batch is a linear continuation from the existing tip.
    // Just validate and move the whole batch into the store
    for (protocol::BlockHeader& header : *batch) {
      const auto [valid, total_work] =
          validator_.IsDownloadedHeaderValid(header, store_.Size(), store_.Tip());
      if (!valid) {
        // TODO: Fail the validation and disconnect the peer.
        return;
      }
      store_.Push(std::move(header), total_work);
    }
  }
*/
}  // namespace hornet::data
