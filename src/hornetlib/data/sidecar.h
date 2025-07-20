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

// Sidecar is a ChainTree data structure that is specialized to allow metadata elements to be stored
// in a separate object, while keeping the structure identical to a main ChainTree.
template <typename T>
class Sidecar : public SidecarBase {
 public:
  Sidecar(const T& default_value = T{}) : default_(default_value) {}

  // Retrieves a value via a locator given by the master ChainTree.
  const T* Get(Locator locator) const {
    const auto it = tree_.Find(locator);
    return it ? &*it : nullptr;
  }

  // Overwrite an existing element of the sidecar with a given value.
  bool Set(Locator locator, const T& value) {
    const auto it = tree_.Find(locator);
    if (!it) return false;
    *it = value;
    return true;
  }

  // Add a new element to the data structure, keeping it in sync with a master ChainTree.
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
