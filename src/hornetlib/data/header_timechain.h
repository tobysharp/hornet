// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <ranges>
#include <tuple>

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/data/hashed_tree.h"
#include "hornetlib/data/header_chain.h"
#include "hornetlib/data/header_context.h"

namespace hornet::data {

class HeaderTimechain {
 public:
  // Public types
  class ValidationView;
  template <bool kIsConst>
  class AncestorIterator;
  using ParentIterator = AncestorIterator<true>;
  using FindResult = std::pair<ParentIterator, std::optional<HeaderContext>>;

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

  // Public methods
  ParentIterator Add(const HeaderContext& context);
  ParentIterator Add(const HeaderContext& context, ParentIterator parent);
  const protocol::BlockHeader* Find(int height, const protocol::Hash& hash) const;
  FindResult Find(const protocol::Hash& hash);
  FindResult HeaviestTip() const;
  std::unique_ptr<ValidationView> GetValidationView(const ParentIterator& tip) const;
  const HeaderChain& HeaviestChain() const {
    return chain_;
  }
  int GetHeaviestTipHeight() const {
    return chain_.GetTipHeight();
  }
  int GetHeaviestLength() const {
    return chain_.Length();
  }

 private:
  using HeaderTree = HashedTree<NodeData>;
  using TreeIterator = HeaderTree::Iterator;
  using TreeNode = HeaderTree::Node;

  // Tree helpers
  TreeNode* AddChild(TreeNode* parent, const HeaderContext& context);
  void PruneReorgTree();
  bool IsValidNode(TreeIterator it) const {
    return tree_.IsValidNode(it);
  }

  // Reorg between tree and chain
  void ReorgBranchToChain(TreeNode* tip);

  // Navigation
  ParentIterator NullIterator() const;
  ParentIterator BeginChain(int height) const;
  ParentIterator BeginTree(TreeIterator node) const;
  ParentIterator BeginTree(TreeNode* node) const;
  const protocol::BlockHeader& GetAncestorAtHeight(ParentIterator tip, int height) const;
  auto AncestorsToHeight(ParentIterator start, int end_height) const;

  // As potential forks or re-orgs are resolved, the heaviest chain is kept in a linear array.
  HeaderChain chain_;

  // Recent headers are kept in a forest of putative chains, with tracked proof-of-work in
  // HeaderContext.
  HeaderTree tree_;
  int min_root_height_;  // The current minimum height among all roots in the tree.

  // Behavior tuning variables
  int max_search_depth_;  // The maximum number of elements to search when looking for a fork point.
  int max_keep_depth_;    // The maximum depth of branches to keep in the tree when pruning.
};

// Ancestor iterator for walking up from a tip to an exclusive height
template <bool kIsConst>
class HeaderTimechain::AncestorIterator {
 public:
  // C++20 iterator traits
  using iterator_concept = std::forward_iterator_tag;
  using value_type = protocol::BlockHeader;
  using pointer = std::conditional_t<kIsConst, const protocol::BlockHeader, protocol::BlockHeader>*;
  using reference =
      std::conditional_t<kIsConst, const protocol::BlockHeader, protocol::BlockHeader>&;
  using difference_type = std::ptrdiff_t;

  // Constructors
  AncestorIterator() : timechain_(nullptr), node_(), height_(-1) {}
  AncestorIterator(const HeaderTimechain& timechain, TreeNode* tip = nullptr, int height = -1)
      : timechain_(&timechain), node_(&*tip), height_(height) {}
  AncestorIterator(const HeaderTimechain& timechain, int height)
      : AncestorIterator(timechain, nullptr, height) {}
  AncestorIterator(const AncestorIterator& rhs) = default;
  AncestorIterator(AncestorIterator&&) = default;

  // Default operators
  AncestorIterator& operator=(const AncestorIterator&) = default;
  AncestorIterator& operator=(AncestorIterator&&) = default;
  bool operator!=(const AncestorIterator& rhs) const = default;
  bool operator==(const AncestorIterator& rhs) const = default;

  // Custom operators
  operator bool() const {
    return IsValid();
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
      if (node_->parent != nullptr) height_ = node_->data.Height();
      node_ = node_->parent;
    } else if (InChain())
      --height_;
    return *this;
  }
  AncestorIterator operator++(int) {
    AncestorIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  // Public methods
  std::optional<value_type> TryGet() const {
    if (InChain())
      return timechain_->chain_[height_];
    else if (InTree())
      return node_->data.Header();
    else
      return {};
  }
  bool IsValid() const {
    return InChain() || InTree();
  }
  int GetHeight() const {
    return InTree() ? node_->data.Height() : height_;
  }

 protected:
  // Protected types and methods are internal to the enclosing HeaderTimechain class.
  friend HeaderTimechain;

  bool InChain() const {
    return height_ >= 0;
  }
  bool InTree() const {
    return node_ != nullptr;
  }
  int ChainHeight() const {
    return height_;
  }
  TreeNode* Node() const {
    return node_;
  }

 private:
  // Private data is internal to this class.
  const HeaderTimechain* timechain_;
  TreeNode* node_;
  int height_;
};

class HeaderTimechain::ValidationView : public consensus::HeaderAncestryView {
 public:
  ValidationView(const HeaderTimechain& timechain, ParentIterator tip)
      : timechain_(timechain), tip_(tip) {}

  void SetTip(ParentIterator tip) {
    tip_ = tip;
  }
  virtual int Length() const override;
  virtual uint32_t TimestampAt(int height) const override;
  virtual std::vector<uint32_t> LastNTimestamps(int count) const override;

 private:
  const HeaderTimechain& timechain_;
  ParentIterator tip_;
};

}  // namespace hornet::data
