// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <vector>

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/data/chain_tree.h"
#include "hornetlib/model/header_context.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::data {

class HeaderTimechain : public ChainTree<protocol::BlockHeader, model::HeaderContext> {
 public:
  // Public types
  class ValidationView;
  template <bool kIsConst> class ContextIterator;
  using Iterator = ContextIterator<false>;
  using ConstIterator = ContextIterator<true>;
  using HeaderContext = model::HeaderContext;
  using Base = ChainTree<protocol::BlockHeader, HeaderContext>;
  using BaseIterator = Base::Iterator;
  using BaseConstIterator = Base::ConstIterator;
  struct AddResult;

  // Public methods
  AddResult Add(const HeaderContext& context);
  AddResult Add(ConstIterator parent, const HeaderContext& context);
  ConstIterator Search(const protocol::Hash& hash) const;
  Iterator Search(const protocol::Hash& hash);
  ConstIterator FindTipOrForks(const protocol::Hash& hash) const;
  Iterator FindTipOrForks(const protocol::Hash& hash);
  ConstIterator ChainTip() const;
  Iterator ChainTip();
  const protocol::Hash& GetChainHash(int height) const;
  std::unique_ptr<ValidationView> GetValidationView(BaseConstIterator tip) const;
  std::optional<Locator> MakeLocator(int height, const protocol::Hash& hash) const;
  BaseConstIterator FindStable(int height, const protocol::Hash& hash) const;

  // Iterate over all known headers from the genesis onward, calling the predicate for each, with arguments
  //  (const Locator& parent, const Key& child, const protocol::BlockHeader& header).  
  template <typename Predicate>
  void ForEach(Predicate predicate) const;

 private:
  struct HeaderContextPolicy {
    const HeaderTimechain* timechain_ = nullptr;
    const protocol::Hash& GetChainHash(int height) const { return timechain_->GetChainHash(height); }
    HeaderContext Extend(const HeaderContext& parent, const protocol::BlockHeader& next) const {
      return parent.Extend(next, GetChainHash(parent.height + 1));
    }    
    HeaderContext Rewind(const HeaderContext& child, const protocol::BlockHeader& prev) const {
      return child.Rewind(prev);
    }
  };
  HeaderContextPolicy GetPolicy() const { return HeaderContextPolicy{this}; }
  Iterator MakeContextIterator(FindResult find);
  ConstIterator MakeContextIterator(ConstFindResult find) const;
  AddResult PromoteBranch(BaseIterator tip);
  void PruneForest();

  // Behavior tuning variables
  int max_search_depth_ = 144;  // The maximum number of elements to search when looking for a fork point.
  int max_keep_depth_ = 288;    // The maximum depth of branches to keep in the tree when pruning.
};

// Iterate over all known headers from the genesis onward, calling the predicate for each, with arguments
//    (const Locator& parent, const Key& child, const protocol::BlockHeader& header).  
template <typename Predicate>
inline void HeaderTimechain::ForEach(Predicate predicate) const {
  // Iterates over main chain headers.
  for (int height = 0; height < std::ssize(chain_); ++height)
    predicate(Locator{height - 1}, {height, GetChainHash(height)}, chain_[height]);

  // Iterates over forest headers.
  for (const auto& node : forest_.ForwardFromOldest()) {
    const bool is_root = node.parent == nullptr;
    const Locator parent = is_root ? Locator{node.data.Height() - 1} : Locator{node.parent->hash};
    predicate(parent, {node.data.Height(), node.hash}, node.data.Data());
  }
}

template <bool kIsConst>
class HeaderTimechain::ContextIterator {
 public:
  // C++20 iterator traits
  using iterator_concept = std::forward_iterator_tag;
  using value_type = HeaderContext;
  using pointer = const HeaderContext*;
  using reference = const HeaderContext&;
  using difference_type = std::ptrdiff_t;
  
  using ChainTreeIterator = AncestorIterator<kIsConst>;

  ContextIterator() = default;
  ContextIterator(const ContextIterator&) = default;
  ContextIterator(ChainTreeIterator base, const HeaderContext& context, const HeaderContextPolicy& policy) 
    : base_(base), context_(context), policy_(policy) {}
  
  template <bool kIsRhsConst>
  requires (kIsConst && !kIsRhsConst)
  ContextIterator(const ContextIterator<kIsRhsConst>& rhs) 
    : base_(rhs.base_), context_(rhs.context_), policy_(rhs.policy_) {}

  const HeaderContext& operator*() const { return context_; }
  const HeaderContext* operator->() const { return &context_; }

  ContextIterator& operator=(const ContextIterator&) = default;
  ContextIterator& operator=(ContextIterator&&) = default;
  bool operator!=(const ContextIterator& rhs) const { return base_ != rhs.base_; }
  bool operator==(const ContextIterator& rhs) const { return base_ == rhs.base_; }

  ContextIterator& operator++() {
    ++base_;
    context_ = policy_.Rewind(context_, base_ ? *base_ : protocol::BlockHeader{});
    return *this;
  }
  operator bool() const { return base_; }
  operator const ChainTreeIterator&() const { return base_; }

  Locator Locator() const {
    return base_.MakeLocator(context_.hash);
  }

 private:
  template <bool> friend class ContextIterator;

  ChainTreeIterator base_;
  HeaderContext context_;
  HeaderContextPolicy policy_;
};

struct HeaderTimechain::AddResult {
  Iterator it;
  std::vector<protocol::Hash> moved_from_chain;
};

class HeaderTimechain::ValidationView : public consensus::HeaderAncestryView {
 public:
  ValidationView(const HeaderTimechain& timechain, BaseConstIterator tip)
      : timechain_(timechain), tip_(tip) {}

  void SetTip(BaseConstIterator tip) {
    tip_ = tip;
  }
  virtual int Length() const override;
  virtual uint32_t TimestampAt(int height) const override;
  virtual std::vector<uint32_t> LastNTimestamps(int count) const override;

 private:
  const HeaderTimechain& timechain_;
  BaseConstIterator tip_;
};

}  // namespace hornet::data
