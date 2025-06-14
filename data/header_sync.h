#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "data/header_context.h"
#include "data/header_chain.h"
#include "protocol/block_header.h"
#include "protocol/work.h"
#include "util/hashed_tree.h"
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
  // also performs the maturation from the tree structure to the header chain.
  void Validate(const util::Timeout& timeout = util::Timeout::Infinite());

 private:
  using HeaderTree = util::HashedTree<HeaderContext>;
  using tree_iterator = HeaderTree::up_iterator;
  using node_iterator = HeaderTree::node_iterator;
  using Batch = std::vector<protocol::BlockHeader>;

  void PopulateReorgTreeFromChain();
  void PruneReorgTree();
  bool IsHeightHistoric(int height) const;
  bool IsNodeInChain(tree_iterator it) const;
  void PerformReorg(tree_iterator batch_parent, node_iterator batch_begin);
  bool ValidateAndAppendToChain(const Batch& batch);
  std::tuple<bool, HeaderSync::tree_iterator> ValidateAndAppendToReorgTree(
      const Batch& batch, tree_iterator parent_node_it);
  void Fail() {}  // TODO

  template <typename Callback>
  bool ValidateBatch(const std::optional<HeaderContext>& parent, const Batch& batch,
                     Callback&& on_valid) const;

  // Headers start out being pushed into an unverified queue pipeline.
  util::ThreadSafeQueue<Batch> queue_;

  // Later, headers are pulled from the unverified queue, verified, and
  // assembled into a tree of putative chains, with tracked proof-of-work.
  HeaderTree tree_;
  static constexpr int kDefaultReorgDepth = 64;
  int depth_for_reorg_ = kDefaultReorgDepth;

  // When potential forks or re-orgs are resolved and the heaviest chain
  // becomes mature, then winning branches from the tree are moved into
  // permanent storage, managed by HeaderChain.
  HeaderChain chain_;
};

}  // namespace hornet::data