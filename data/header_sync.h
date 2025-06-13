#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "data/header_store.h"
#include "protocol/block_header.h"
#include "protocol/work.h"
#include "util/thread_safe_queue.h"

namespace hornet::data {

class HeaderSync {
 public:
  // Push new headers into the unverified queue for later processing, which
  // could be in the same thread or a different thread.
  void Receive(std::span<const protocol::BlockHeader> headers) {  // Copy
    queue_.Push(Batch{headers.begin(), headers.end()});
  }

  void Receive(std::vector<protocol::BlockHeader>&& headers) {  // Move
    queue_.Push(std::move(headers));
  }

  // Returns true if there is validation work to be done, i.e. queued headers.
  bool HasPendingWork() const {
    return !queue_.Empty();
  }

  // Perform validation on the queued headers in the current thread. Works
  // until all work is done or the given timeout (in ms) expires. This method
  // also performs the maturation from the tree structure to the header store.
  void Validate(const util::Timeout& timeout = util::Timeout::Infinite());

 private:
  struct TreeNode;
  using tree_iterator = std::list<TreeNode>::reverse_iterator;
  using node_iterator = std::list<TreeNode>::iterator;
  using Batch = std::vector<protocol::BlockHeader>;

  struct TreeNode {
    tree_iterator parent;
    int height;
    protocol::Work chain_work;
    protocol::BlockHeader header;
    protocol::Hash hash;
  };

  void PopulateReorgTreeFromChain();
  void PruneReorgTree();
  void ClearReorgTree();
  bool IsHeightHistoric(int height) const;
  bool IsNodeInChainStore(tree_iterator it) const;
  void PerformReorg(tree_iterator batch_parent, node_iterator batch_begin);
  bool ValidateAndAppendToChain(const Batch& batch);
  std::tuple<bool, HeaderSync::node_iterator> ValidateAndAppendToReorgTree(
      const Batch& batch, tree_iterator parent_node_it);
  void Fail() {}  // TODO
  bool IsValidNode(tree_iterator iterator) const {
    return iterator != reorg_tree_.rend();
  }
  bool IsValidNode(node_iterator iterator) const {
    return iterator != reorg_tree_.end();
  }
  static node_iterator TreeToNodeIterator(tree_iterator it) {
    return std::prev(it.base());
  }
  
  // Headers start out being pushed into an unverified queue pipeline.
  util::ThreadSafeQueue<Batch> queue_;

  // Later, headers are pulled from the unverified queue, verified, and
  // assembled into a tree of putative chains, with tracked proof-of-work.
  std::list<TreeNode> reorg_tree_;
  std::unordered_map<protocol::Hash, tree_iterator> reorg_map_;
  static constexpr int kDefaultReorgDepth = 64;
  int depth_for_reorg_ = kDefaultReorgDepth;

  // When potential forks or re-orgs are resolved and the heaviest chain
  // becomes mature, then winning branches from the tree are moved into
  // permanent storage, managed by HeaderStore.
  HeaderStore store_;
};

}  // namespace hornet::data