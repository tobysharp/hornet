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

  void Receive(std::vector<protocol::BlockHeader>&& headers) {    // Move
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
  struct TreeNode {
    std::list<TreeNode>::iterator parent;
    int height;
    protocol::Work chain_work;
    protocol::BlockHeader header;
  };

  void PopulateReorgTreeFromChain();
  void PruneReorgTree();
  bool IsHeightHistoric(int height) const;

  // Headers start out being pushed into an unverified queue pipeline.
  using Batch = std::vector<protocol::BlockHeader>;
  util::ThreadSafeQueue<Batch> queue_;

  // Later, headers are pulled from the unverified queue, verified, and
  // assembled into a tree of putative chains, with tracked proof-of-work.
  std::list<TreeNode> reorg_tree_;
  std::unordered_map<protocol::Hash, std::list<TreeNode>::iterator> reorg_map_;

  // When potential forks or re-orgs are resolved and the heaviest chain
  // becomes mature, then winning branches from the tree are moved into
  // permanent storage, managed by HeaderStore.
  HeaderStore store_;
};

}  // namespace hornet::data