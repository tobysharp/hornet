// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <memory>
#include <vector>

#include "hornetlib/consensus/header_ancestry_view.h"
#include "hornetlib/data/chain_tree.h"
#include "hornetlib/data/header_context.h"
#include "hornetlib/protocol/block_header.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::data {

class HeaderTimechain : public ChainTree<protocol::BlockHeader, HeaderContext>{
 public:
  // Public types
  class ValidationView;

  // Public methods
  FindResult Add(const HeaderContext& context);
  FindResult Add(FindResult parent, const HeaderContext& context);
  FindResult Find(const protocol::Hash& hash);
  const protocol::BlockHeader* Find(int height, const protocol::Hash& hash) const;
  std::unique_ptr<ValidationView> GetValidationView(const Iterator& tip) const;

 private:
  using Base = ChainTree<protocol::BlockHeader, HeaderContext>;
  struct HeaderContextPolicy {
    const HeaderTimechain& timechain_;
    const protocol::Hash& GetChainHash(int height) const { return timechain_.GetChainHash(height); }
    HeaderContext Extend(const HeaderContext& parent, const protocol::BlockHeader& next) const {
      return parent.Extend(next, GetChainHash(parent.height + 1));
    }    
    HeaderContext Rewind(const HeaderContext& child, const protocol::BlockHeader& prev) const {
      return child.Rewind(prev);
    }
  };

  const protocol::Hash& GetChainHash(int height) const;
  Iterator PromoteBranch(Iterator tip);
  void PruneForest();

  // Behavior tuning variables
  int max_search_depth_ = 144;  // The maximum number of elements to search when looking for a fork point.
  int max_keep_depth_ = 288;    // The maximum depth of branches to keep in the tree when pruning.
};

class HeaderTimechain::ValidationView : public consensus::HeaderAncestryView {
 public:
  ValidationView(const HeaderTimechain& timechain, Iterator tip)
      : timechain_(timechain), tip_(tip) {}

  void SetTip(Iterator tip) {
    tip_ = tip;
  }
  virtual int Length() const override;
  virtual uint32_t TimestampAt(int height) const override;
  virtual std::vector<uint32_t> LastNTimestamps(int count) const override;

 private:
  const HeaderTimechain& timechain_;
  Iterator tip_;
};

}  // namespace hornet::data
