#pragma once

#include <memory>
#include <ranges>

#include "consensus/header_ancestry_view.h"
#include "data/hashed_tree.h"
#include "data/header_chain.h"
#include "data/header_context.h"

namespace hornet::data {

class HeaderTimechain {
 public:
  class ValidationView;
  class AncestorIterator;

  struct NodeData {
    HeaderContext context;
    int root_height;

    int Height() const {
      return context.height;
    }
    const protocol::BlockHeader& Header() const {
      return context.header;
    }
    const protocol::Work& TotalWork() const {
      return context.total_work;
    }
    const protocol::Hash& GetHash() const {
      return context.hash;
    }
  };

  using HeaderTree = HashedTree<NodeData>;
  using tree_iterator = HeaderTree::up_iterator;

  AncestorIterator Add(HeaderContext context);
  AncestorIterator Add(HeaderContext context, const AncestorIterator& parent);
  AncestorIterator Find(const protocol::Hash& hash);

  AncestorIterator NullPosition() const;
  AncestorIterator BeginChain(int height) const;
  AncestorIterator BeginTree(tree_iterator node) const;
  std::unique_ptr<ValidationView> GetValidationView(const AncestorIterator& tip) const;
  void ReorgBranchToChain(tree_iterator tip);
  void PruneReorgTree();
  HeaderContext RewindRoot(tree_iterator root) const;
  tree_iterator AddChild(tree_iterator parent, HeaderContext context);
  bool IsValidNode(tree_iterator it) const {
    return tree_.IsValidNode(it);
  }
  int GetMinKeepHeight() const {
    return chain_.GetTipHeight() - max_keep_depth_;
  }
  std::optional<protocol::BlockHeader> GetAncestorAtHeight(const AncestorIterator& tip,
                                                           int height) const;
  std::ranges::subrange<AncestorIterator> AncestorsToHeight(const AncestorIterator& start,
                                                            int end_height) const;

  // As potential forks or re-orgs are resolved and the heaviest chain
  // changes, then winning branches from the tree are moved into
  // permanent storage, managed by HeaderChain.
  HeaderChain chain_;

  // Recent headers are kept in a tree of putative chains, with tracked proof-of-work.
  HeaderTree tree_;
  int max_search_depth_;
  int max_keep_depth_;
  int min_root_height_;
};

// Ancestor iterator for walking up from a tip to an exclusive height
class HeaderTimechain::AncestorIterator {
 public:
  using value_type = protocol::BlockHeader;
  using difference_type = std::ptrdiff_t;

  AncestorIterator() : timechain_(nullptr), node_(), height_(-1) {}
  AncestorIterator(const HeaderTimechain& timechain, tree_iterator tip, int height)
      : timechain_(&timechain), node_(tip), height_(height) {}

  // Constructor for element in chain
  AncestorIterator(const HeaderTimechain& timechain, int height)
      : AncestorIterator(timechain, timechain.tree_.NullParent(), height) {}

  // Constructor for tree node
  AncestorIterator(const HeaderTimechain& timechain, tree_iterator tip)
      : AncestorIterator(timechain, tip, -1) {}

  AncestorIterator(const AncestorIterator& rhs)
      : timechain_(rhs.timechain_), node_(rhs.node_), height_(rhs.height_) {}

  AncestorIterator(AncestorIterator&&) = default;
  AncestorIterator& operator=(const AncestorIterator&) = default;
  AncestorIterator& operator=(AncestorIterator&&) = default;

  std::optional<protocol::BlockHeader> TryGet() const {
    if (InChain())
      return timechain_->chain_[height_];
    else if (InTree())
      return node_->data.Header();
    else
      return {};
  }

  const protocol::BlockHeader& operator*() const {
    if (InChain())
      return timechain_->chain_[height_];
    else if (InTree())
      return node_->data.Header();
    else
      util::ThrowRuntimeError("Tried to access a non-existent element.");
  }

  const protocol::BlockHeader* operator->() const {
    return &operator*();
  }

  AncestorIterator& operator++() {
    if (InTree()) {
      if (!timechain_->IsValidNode(node_->parent)) height_ = node_->data.Height();
      node_ = node_->parent;
    } else if (InChain())
      --height_;
    return *this;
  }

  void operator++(int) {
    ++(*this);
  }

  bool operator!=(const AncestorIterator& rhs) const {
    return !operator==(rhs);
  }
  bool operator==(const AncestorIterator& rhs) const {
    return node_ == rhs.node_ && height_ == rhs.height_;
  }
  bool InChain() const {
    return height_ >= 0;
  }
  bool InTree() const {
    return timechain_->IsValidNode(node_);
  }
  bool IsValid() const {
    return InChain() || InTree();
  }
  int ChainHeight() const {
    return height_;
  }
  int GetHeight() const {
    return InChain() ? height_ : (InTree() ? node_->data.Height() : -1);
  }
  tree_iterator Node() const {
    return node_;
  }
  operator bool() const {
    return IsValid();
  }

 private:
  const HeaderTimechain* timechain_;
  tree_iterator node_;
  int height_;
};

class HeaderTimechain::ValidationView : public consensus::HeaderAncestryView {
 public:
  ValidationView(const HeaderTimechain& timechain, const AncestorIterator& tip)
      : timechain_(timechain), tip_(tip) {}

  virtual std::optional<uint32_t> TimestampAt(int height) const override;
  virtual std::vector<uint32_t> LastNTimestamps(int count) const override;

 private:
  const HeaderTimechain& timechain_;
  AncestorIterator tip_;
};

}  // namespace hornet::data
