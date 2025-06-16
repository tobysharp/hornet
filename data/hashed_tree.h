#pragma once

#include <list>
#include <ranges>
#include <unordered_map>

#include "protocol/hash.h"
#include "util/pointer_iterator.h"

namespace hornet::data {

template <typename T, typename Hasher = decltype([](const T& x) { return x.GetHash(); })>
class HashedTree {
 public:
  using Hash = decltype(std::declval<Hasher>()(std::declval<T>()));

  struct Node {
    Node* parent;
    Hash hash;
    T data;
  };

  static constexpr auto get_parent_ = [](Node* node) { return node->parent; };

  using Iterator = std::list<Node>::iterator;
  using ConstIterator = std::list<Node>::const_iterator;
  using UpIterator = util::PointerIterator<Node, decltype(get_parent_), false>;
  using ConstUpIterator = util::PointerIterator<Node, decltype(get_parent_), true>;

  HashedTree(Hasher hasher = [](const T& x) { return x.GetHash(); }) : hasher_(std::move(hasher)) {}

  bool Empty() const {
    return list_.empty();
  }

  bool IsValidNode(const Node* node) const {
    return node != nullptr;
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

  void Clear() {
    list_.clear();
    map_.clear();
  }

  Iterator AddChild(Iterator parent, T data) {
    Hash hash = hasher_(data);
    Node* const parent_node = parent == list_.end() ? nullptr : &*parent;
    list_.emplace_back(Node{parent_node, hash, std::move(data)});
    return map_[hash] = std::prev(list_.end());
  }

  Iterator Erase(Iterator it) {
    map_.erase(it->hash);
    return list_.erase(it);
  }

  Iterator NullIterator() {
    return list.end();
  }

  ConstIterator NullIterator() const {
    return list.cend();
  }

  const TreeNode& Oldest() const {
    return list_.front();
  }

  const TreeNode& Latest() const {
    return list_.back();
  }

  auto ForwardFromOldest() {
    return std::ranges::subrange{list_.begin(), list_.end()};
  }

  auto BackFromLatest() {
    return FromNode(list_.empty() ? nullptr : &list_.back());
  }

  auto UpFromNode(Iterator it) {
    return std::ranges::subrange{UpIterator{&*it}, nullptr};
  }

  auto UpFromNode(ConstIterator it) const {
    return std::ranges::subrange{ConstUpIterator{&*it}, nullptr};
  }

 private:
  std::list<Node> list_;
  std::unordered_map<Hash, node_iterator> map_;
  Hasher hasher_;
};

}  // namespace hornet::data
