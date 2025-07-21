// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <span>

#include "hornetlib/data/chain_tree.h"
#include "hornetlib/protocol/hash.h"

namespace hornet::data {

// SidecarSync facilitates adding new entries to a Sidecar, while keeping its structure coupled
// to exactly match other ChainTree instances.
struct SidecarAddSync {
  Locator parent;
  protocol::Hash hash;
  std::span<const protocol::Hash> moved_from_chain;
};

class SidecarBase {
 public:
  virtual ~SidecarBase() = default;
  virtual void AddSync(const SidecarAddSync& sync) = 0;
};

template <typename T>
class SidecarBaseT : public SidecarBase {
 public:
  virtual void Set(const Locator& locator, const T& value) = 0;
  virtual const T* Get(const Locator&) const = 0;
};

// Sidecar is a ChainTree data structure that is specialized to allow metadata elements to be stored
// in a separate object, while keeping the structure identical to a driving ChainTree.
template <typename T>
class Sidecar : public SidecarBaseT<T> {
 public:
  Sidecar(const T& default_value = T{}) : default_(default_value) {}

  bool Empty() const {
    return tree_.Empty();
  }

  int Size() const {
    return tree_.Size();
  }

  int ChainLength() const {
    return tree_.ChainLength();
  }

  // Retrieves a value via a locator given by the driving ChainTree.
  virtual const T* Get(const Locator& locator) const override {
    const auto it = tree_.Find(locator);
    return it ? &*it : nullptr;
  }

  // Overwrite an existing element of the sidecar with a given value.
  virtual void Set(const Locator& locator, const T& value) override {
    const auto it = tree_.Find(locator);
    Assert(it);
    if (it)
      *it = value;
  }

  // Add a new element to the data structure, keeping it in sync with a driving ChainTree.
  virtual void AddSync(const SidecarAddSync& sync) override {
    const auto parent_it = tree_.Find(sync.parent);  // Zero lookups if in main chain.
    const typename Tree::Context context = { default_, sync.hash, parent_it.GetHeight() + 1 };
    auto child_it = tree_.Add(parent_it, context);
    if (!sync.moved_from_chain.empty())
      child_it = tree_.PromoteBranch(child_it, sync.moved_from_chain).first;
  }

 private:
  using Tree = ChainTree<T>;
  Tree tree_;
  T default_;
};

}  // namespace hornet::data
