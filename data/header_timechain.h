#pragma once

#include <memory>

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

    int Height() const { return context.height; }
    const protocol::BlockHeader& Header() const { return context.header; }
    const protocol::Work& TotalWork() const { return context.total_work; }
  };

  using HeaderTree = HashedTree<NodeData>;
  using tree_iterator = HeaderTree::up_iterator;

  struct Position {
    tree_iterator node = {};
    int chain_height = -1;

    bool InChain() const { return chain_height >= 0; }
    bool InTree(const HeaderTree& tree) const { return tree.IsValidNode(node); }
    bool IsValid(const HeaderTree& tree) const { return InChain() || InTree(tree); }
  };

  Position Add(HeaderContext context);
  Position Add(HeaderContext context, const Position& parent);
  
  Position NullPosition() const { return {tree_.NullParent(), -1}; }
  bool IsValid(const Position& position) const { return position.IsValid(tree_); }

  std::unique_ptr<ValidationView> GetValidationView() const;

 private:
  void ReorgBranchToChain(tree_iterator tip);
  void PruneReorgTree();
  HeaderContext RewindRoot(tree_iterator root) const;
  tree_iterator AddChild(tree_iterator parent, HeaderContext context);
  bool IsValidNode(tree_iterator it) const { return tree_.IsValidNode(it); }
  int GetMinKeepHeight() const { return chain_.GetTipHeight() - max_keep_depth_; }

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

class HeaderTimechain::ValidationView : public consensus::HeaderAncestryView {
 public:
  ValidationView(const HeaderTimechain& timechain, tree_iterator tip)
      : timechain_(timechain), tip_(tip) {}

  virtual std::optional<uint32_t> TimestampAt(int height) const override;
  virtual std::vector<uint32_t> LastNTimestamps(int count) const override;

 private:
  const HeaderTimechain& timechain_;
  tree_iterator tip_;
};

// Ancestor iterator for walking up from a tip to an exclusive height
class HeaderTimechain::AncestorIterator {
 public:
  // Constructor for chain tip
  AncestorIterator(const HeaderTimechain& timechain)
      : AncestorIterator(timechain, timechain.chain_.GetTipHeight()) {
    AssertValid();
  }

  // Constructor for element in chain
  AncestorIterator(const HeaderTimechain& timechain, int height)
      : timechain_(timechain), node_(timechain.tree_.NullParent()), height_(height) {
    AssertValid();
  }

  // Constructor for tree node
  AncestorIterator(const HeaderTimechain& timechain, tree_iterator tip)
      : timechain_(timechain),
        node_(tip),
        height_(timechain.tree_.IsValidNode(tip) ? tip->data.Height()
                                                 : timechain.chain_.GetTipHeight()) {
    AssertValid();
  }

  bool OnChain() const {
    return !timechain_.tree_.IsValidNode(node_) && height_ >= 0 &&
           height_ < timechain_.chain_.Length();
  }

  std::optional<protocol::BlockHeader> TryGet() const {
    if (timechain_.tree_.IsValidNode(node_)) return node_->data.Header();
    if (height_ >= 0 && height_ < timechain_.chain_.Length()) return timechain_.chain_[height_];
    return {};
  }

  const protocol::BlockHeader& operator*() const {
    if (timechain_.tree_.IsValidNode(node_)) return node_->data.Header();
    if (height_ < 0 || height_ < timechain_.chain_.Length())
      util::ThrowRuntimeError("Tried to access a non-existent element.");
    return timechain_.chain_[height_];
  }

  const protocol::BlockHeader* operator->() const {
    return &operator*();
  }

  AncestorIterator& operator++() {
    --height_;
    if (timechain_.tree_.IsValidNode(node_)) node_ = node_->parent;
    AssertValid();
    return *this;
  }

  bool operator!=(const AncestorIterator& rhs) const {
    return height_ != rhs.height_;
  }

 private:
  void AssertValid() const {
    Assert(!timechain_.tree_.IsValidNode(node_) || node_->data.Height() == height_);
  }

  const HeaderTimechain& timechain_;
  tree_iterator node_;
  int height_;
};

}  // namespace hornet::data
