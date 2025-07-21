// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/data/sidecar.h"

namespace hornet::data {

// KeyframeSidecar is a specialized sidecar data structure for piecewise-continuous metadata,
// i.e. a property that takes on the same value for long stretches of the timechain. Internally,
// these values are optimized for memory use by representing them as run-length encoded spans,
// similar to keyframes in graphics/animation. This class can be useful for representing linearly
// progressing state like block validation state during initial block download.
//
// Like all sidecars, KeyframeSidecar is able to maintain its structure in lockstep with a driving
// ChainTree, abstracting the complexity of occasional reorgs between the active chain and its forks.
// This allows metadata to be stored separately from the header timechain, while remaining in sync.
// It also permits memory optimizations like this one for KeyframeSidecar.
template <typename T>
class KeyframeSidecar : public SidecarBaseT<T> {
 public:
  KeyframeSidecar(const T& default_value = T{})
    : default_(default_value), length_(0) {
    }
  
  bool Empty() const {
    return length_ == 0;
  }

  int ChainLength() const {
    return length_;
  }

  int Size() const {
    return length_ + forks_.Size();
  }

  virtual const T* Get(const Locator& locator) const override {
    if (std::holds_alternative<int>(locator)) {
      // O(log N) binary search on keyframes.
      const int height = std::get<int>(locator);
      if (height < 0 || height >= length_) return nullptr;
      
      const auto it = std::prev(FirstKeyframeAfter(height));
      return &it->value;
    } else {
      // O(1) lookup in forks hash map.
      const protocol::Hash& hash = std::get<protocol::Hash>(locator);
      const auto it = forks_.Find(hash);
      return forks_.IsValidNode(it) ? &(it->data.value) : nullptr;
    }
  }

  virtual void Set(const Locator& locator, const T& value) override {
    if (std::holds_alternative<int>(locator)) {
      const int height = std::get<int>(locator);
      Assert(height < length_);
      if (height >= length_) 
        return;

      // Find the first keyframe after the relevant height.
      auto next = FirstKeyframeAfter(height);
      auto current = std::prev(next);  // Adjust to containing keyframe
      const int current_end = (next == keyframes_.end()) ? length_ : next->start;

      if (current->value == value)
        return;  // Nothing to do.
      if (current->start == height) {
        // If the current keyframe has size 1 anyway, we can just overwrite it.
        if (current->start + 1 == current_end) {
          current->value = value;
          // But before returning, we need to check if the current keyframe now matches its neighbors.
          if (next != keyframes_.end() && current->value == next->value)
            next = keyframes_.erase(next);
          if (current != keyframes_.begin() && current->value == std::prev(current)->value)
            current = keyframes_.erase(current);
        } else {
          // Move the current start up by one position to become the second half of the split keyframe.
          ++(current->start);
          // If the value extends the previous keyframe, we are done.
          // Otherwise, insert a new keyframe for the first half of the split.
          if (current == keyframes_.begin() || std::prev(current)->value != value)
            keyframes_.insert(current, {height, value});
        }
      } else {
        // In the general case, we may need to split a keyframe into three parts, e.g.
        // [0, "old"] --> [0, "old"], [height, "value"], [height + 1, "old"].

        // If we're not touching the end of the current keyframe, insert the upper third split.
        if (height + 1 < current_end)
          next = keyframes_.insert(next, {height + 1, current->value});
        // Insert the central third split.
        keyframes_.insert(next, {height, value});
      }
    } else {
      auto it = forks_.Find(std::get<protocol::Hash>(locator));
      Assert(forks_.IsValidNode(it));
      if (forks_.IsValidNode(it)) 
        it->data.value = value;
    }
  }

  virtual void AddSync(const SidecarAddSync& sync) override {
    // Add the new element to the parent.
    typename Tree::Iterator tip = forks_.NullIterator();
    if (std::holds_alternative<int>(sync.parent)) {
      // Parent is on the main chain.
      const int parent_height = std::get<int>(sync.parent);
      if (parent_height == length_ - 1) {
        ++length_;  // Adding an element to the chain tip.
        if (keyframes_.empty() || keyframes_.back().value != default_)
          keyframes_.push_back({length_ - 1, default_});
      }
      else {
        // Forking from the main chain.
        tip = forks_.AddChild(nullptr, {default_, sync.hash, parent_height + 1});
      }
    } else {
      // Parent is a fork.
      auto it = forks_.Find(std::get<protocol::Hash>(sync.parent));
      tip = forks_.AddChild(&*it, {default_, sync.hash, it->data.height + 1});
    }

    // If specified, promote the branch to the main chain.
    if (!sync.moved_from_chain.empty())
      PromoteBranch(tip, sync.moved_from_chain);
  }

 private:
  struct Keyframe {
    int start;
    T value;
  };
  struct NodeData {
    T value;
    protocol::Hash hash;
    int height;

    const protocol::Hash& GetHash() const { return hash; }
  };
  using Tree = HashedTree<NodeData>;

  auto FirstKeyframeAfter(int height) const {
    return std::upper_bound(keyframes_.begin(), keyframes_.end(), height,
        [](int h, const Keyframe& keyframe) { return h < keyframe.start; });
  }

  auto FirstKeyframeAfter(int height) {
    return std::upper_bound(keyframes_.begin(), keyframes_.end(), height,
        [](int h, const Keyframe& keyframe) { return h < keyframe.start; });
  }

  // This method follows the pattern set out in ChainTree::PromoteBranch.
  void PromoteBranch(Tree::Iterator tip, std::span<const protocol::Hash> moved_from_chain) {
    using Node = HashedTree<NodeData>::Node;
    Assert(forks_.IsValidNode(tip));
    Node* tip_node = &*tip;
    Assert(forks_.IsLeaf(tip_node));

    // Locate the branch root in the tree.
    std::stack<Node*> stack;
    for (Node& node : forks_.UpFromNode(tip_node)) stack.push(&node);
    const auto root = stack.top();

    // Find the fork point in the chain.
    int fork_height = root->data.height - 1;
    Assert(length_ - (fork_height + 1) == std::ssize(moved_from_chain));
  
    // Copy the old chain elements into the tree.
    Node* parent = nullptr;
    for (int height = fork_height + 1; height < length_; ++height) {
      const T chain_value = *Get(height);
      const auto& hash = moved_from_chain[height - fork_height - 1];
      parent = &*forks_.AddChild(parent, {chain_value, hash, height});
    }

    // Truncate the chain back to the common ancestor fork point.
    keyframes_.erase(FirstKeyframeAfter(fork_height), keyframes_.end());
    length_ = fork_height + 1;

    // Now walk forward down the new branch, moving elements into the main chain.
    for (; !stack.empty(); stack.pop()) {
      ++length_;
      Set(stack.top()->data.height, stack.top()->data.value);
    }

    // Finally delete the chain containing the new tip.
    forks_.EraseChain(tip_node);
  }

  T default_;
  int length_;
  std::vector<Keyframe> keyframes_;
  Tree forks_;
};

}  // namespace hornet::data
