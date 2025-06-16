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
  template <bool kIsConst> class AncestorIterator;

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

  using ParentIterator = AncestorIterator<false>;
  using ConstParentIterator = AncestorIterator<true>;

  ParentIterator Add(HeaderContext context);
  ParentIterator Add(HeaderContext context, const ParentIterator& parent);

  struct FindResult;
  FindResult Find(const protocol::Hash& hash);

  std::unique_ptr<ValidationView> GetValidationView(const ParentIterator& tip) const;

private:
  using HeaderTree = HashedTree<NodeData>;
  using TreeIterator = HeaderTree::Iterator;
  using ConstTreeIterator = HeaderTree::ConstIterator;

  ParentIterator NullIterator() const;
  ParentIterator BeginChain(int height) const;
  ParentIterator BeginTree(TreeIterator node) const;

  void ReorgBranchToChain(TreeIterator tip);
  void PruneReorgTree();
  HeaderContext RewindRoot(TreeIterator root) const;
  TreeIterator AddChild(TreeIterator parent, HeaderContext context);
  bool IsValidNode(ConstTreeIterator it) const {
    return tree_.IsValidNode(it);
  }  
  bool IsValidNode(TreeIterator it) const {
    return tree_.IsValidNode(it);
  }
  bool IsValidNode(const HeaderTree::Node* node) const {
    return tree_.IsValidNode(node);
  }
  int GetMinKeepHeight() const {
    return chain_.GetTipHeight() - max_keep_depth_;
  }
  const protocol::BlockHeader& GetAncestorAtHeight(const ConstParentIterator& tip,
                                                           int height) const;
  auto AncestorsToHeight(const ConstParentIterator& start, int end_height) const;

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
template <bool kIsConst>
class HeaderTimechain::AncestorIterator {
 public:
  using Iterator = std::conditional_t<kIsConst, ConstTreeIterator, TreeIterator>;
  using value_type = std::conditional_t<kIsConst, const protocol::BlockHeader, protocol::BlockHeader>;
  using difference_type = std::ptrdiff_t;

  AncestorIterator() : timechain_(nullptr), node_(), height_(-1) {}
  AncestorIterator(const HeaderTimechain& timechain, ConstTreeIterator tip, int height)
      : timechain_(&timechain), node_(tip), height_(height) {}

  // Constructor for element in chain
  AncestorIterator(const HeaderTimechain& timechain, int height)
      : AncestorIterator(timechain, timechain.tree_.NullIterator(), height) {}

  // Constructor for tree node
  AncestorIterator(const HeaderTimechain& timechain, ConstTreeIterator tip)
      : AncestorIterator(timechain, tip, -1) {}

  AncestorIterator(const AncestorIterator& rhs)
      : timechain_(rhs.timechain_), node_(rhs.node_), height_(rhs.height_) {}

  AncestorIterator(AncestorIterator&&) = default;
  AncestorIterator& operator=(const AncestorIterator&) = default;
  AncestorIterator& operator=(AncestorIterator&&) = default;

  std::optional<value_type> TryGet() const {
    if (InChain())
      return timechain_->chain_[height_];
    else if (InTree())
      return node_->data.Header();
    else
      return {};
  }

  reference operator*() const {
    if (InChain())
      return timechain_->chain_[height_];
    else if (InTree())
      return node_->data.Header();
    else
      util::ThrowRuntimeError("Tried to access a non-existent element.");
  }

  pointer operator->() const {
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
  Iterator Node() const {
    return node_;
  }
  operator bool() const {
    return IsValid();
  }

 private:
  const HeaderTimechain* timechain_;
  Iterator node_;
  int height_;
};

struct HeaderTimechain::FindResult {
  ParentIterator iterator;
  std::optional<HeaderContext> context;
};

class HeaderTimechain::ValidationView : public consensus::HeaderAncestryView {
 public:
  ValidationView(const HeaderTimechain& timechain, const ConstParentIterator& tip)
      : timechain_(timechain), tip_(tip) {}

  virtual std::optional<uint32_t> TimestampAt(int height) const override;
  virtual std::vector<uint32_t> LastNTimestamps(int count) const override;

 private:
  const HeaderTimechain& timechain_;
  ConstParentIterator tip_;
};

}  // namespace hornet::data
