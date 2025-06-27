// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <list>
#include <ranges>
#include <unordered_map>

#include "hornetlib/protocol/hash.h"
#include "hornetlib/util/pointer_iterator.h"

namespace hornet::data {

struct GetHashFunctor {
  template <typename T>
  protocol::Hash operator()(const T& x) {
    return x.GetHash();
  }
};

template <typename T, typename Hasher = GetHashFunctor>
class HashedTree {
 public:
  using Hash = decltype(std::declval<Hasher>()(std::declval<T>()));

  struct Node {
    Node* parent;
    Hash hash;
    T data;
  };

  struct GetParent {
    Node* operator()(Node* node) const {
      return node->parent;
    }
    const Node* operator()(const Node* node) const {
      return node->parent;
    }
  };

  using Iterator = std::list<Node>::iterator;
  using ConstIterator = std::list<Node>::const_iterator;
  using UpIterator = util::PointerIterator<Node, GetParent, false>;
  using ConstUpIterator = util::PointerIterator<Node, GetParent, true>;

  HashedTree(Hasher&& hasher = GetHashFunctor{}) : hasher_(std::forward<Hasher>(hasher)) {}

  bool Empty() const {
    return list_.empty();
  }

  bool IsValidNode(Iterator iterator) const {
    return iterator != list_.end();
  }

  bool IsValidNode(ConstIterator iterator) const {
    return iterator != list_.cend();
  }

  ConstIterator Find(const protocol::Hash& hash) const {
    const auto it = map_.find(hash);
    return it == map_.end() ? list_.cend() : it->second;
  }

  Iterator Find(const protocol::Hash& hash) {
    const auto it = map_.find(hash);
    return it == map_.end() ? list_.end() : it->second;
  }

  bool IsLeaf(const Node* node) const {
    return child_map_.find(node) == child_map_.end();
  }

  void Clear() {
    list_.clear();
    map_.clear();
  }

  Iterator AddChild(Node* parent, T data) {
    const Hash hash = hasher_(data);
    list_.emplace_back(Node{parent, hash, std::move(data)});
    const Iterator it = std::prev(list_.end());
    map_[hash] = it;
    if (parent != nullptr) child_map_.insert({parent, it});
    return it;
  }

  Iterator Erase(Iterator it) {
    // Handle the case where this node has a parent
    if (it->parent != nullptr) {
      const auto siblings = child_map_.equal_range(it->parent);
      for (auto sibling = siblings.first; sibling != siblings.second; ++sibling) {
        if (sibling->second == it) {
          child_map_.erase(sibling);
          break;
        };
      }
    }

    // Handle the case where this node has orphaned children
    {
      const auto children = child_map_.equal_range(&*it);
      for (auto child = children.first; child != children.second; ++child)
        child->second->parent = nullptr;
      child_map_.erase(&*it);
    }

    map_.erase(it->hash);
    return list_.erase(it);
  }

  void EraseChain(Node* leaf) {
    Assert(child_map_.find(leaf) == child_map_.end());

    Node* next = nullptr;
    // Iterate up the chain
    for (Node* node = leaf; node != nullptr; node = next) {
      // Promote all children of the parent node
      if ((next = node->parent) != nullptr) {
        const auto siblings = child_map_.equal_range(next);
        for (auto sibling = siblings.first; sibling != siblings.second; ++sibling)
          sibling->second->parent = nullptr;
        child_map_.erase(next);
      }

      // Delete the current node
      const auto map_it = map_.find(node->hash);
      map_.erase(map_it);
      list_.erase(map_it->second);
    }
  }

  Iterator NullIterator() {
    return list_.end();
  }

  ConstIterator NullIterator() const {
    return list_.cend();
  }

  const Node& Oldest() const {
    return list_.front();
  }

  const Node& Latest() const {
    return list_.back();
  }

  auto ForwardFromOldest() {
    return std::ranges::subrange{list_.begin(), list_.end()};
  }

  auto BackFromLatest() {
    return FromNode(list_.empty() ? nullptr : &list_.back());
  }

  auto UpFromNode(Node* node) {
    static_assert(std::forward_iterator<UpIterator>);
    return std::ranges::subrange{UpIterator{node}, UpIterator{nullptr}};
  }

  auto UpFromNode(const Node* node) const {
    return std::ranges::subrange{ConstUpIterator{node}, nullptr};
  }

 private:
  std::list<Node> list_;
  std::unordered_map<Hash, Iterator> map_;
  std::unordered_multimap<const Node*, Iterator> child_map_;
  Hasher hasher_;
};

}  // namespace hornet::data
