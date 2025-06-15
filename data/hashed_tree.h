#pragma once

#include <list>
#include <unordered_map>

#include "protocol/hash.h"

namespace hornet::data {

template <typename T, typename Hasher = decltype([](const T& x) { return x.GetHash(); })>
class HashedTree {
 public:
  using Hash = decltype(std::declval<Hasher>()(std::declval<T>()));
  struct TreeNode;
  using node_iterator = std::list<TreeNode>::iterator;
  using up_iterator = std::list<TreeNode>::reverse_iterator;

  struct TreeNode {
    up_iterator parent;
    Hash hash;
    T data;
  };

  HashedTree(Hasher hasher = [](const T& x) { return x.GetHash(); }) : hasher_(std::move(hasher)) {}

  bool Empty() const {
    return list_.empty();
  }
  bool IsValidNode(up_iterator iterator) const {
    return iterator != list_.rend();
  }
  bool IsValidNode(node_iterator iterator) const {
    return iterator != list_.end();
  }

  up_iterator Find(const protocol::Hash& hash) {
    const auto it = map_.find(hash);
    return it == map_.end() ? list_.rend() : it->second;
  }
  void Clear() {
    list_.clear();
    map_.clear();
  }
  up_iterator AddChild(up_iterator parent, T data) {
    const Hash hash = hasher_(data);
    list_.emplace_back(TreeNode{parent, hash, std::move(data)});
    return (map_[hash] = list_.rbegin());
  }
  node_iterator Erase(tree_iterator it) {
    map_.erase(it->hash);
    return list_.erase(it);
  }
  static node_iterator UpToNodeIterator(up_iterator it) {
    return std::prev(it.base());
  }
  up_iterator NullParent() const {
    return list_.rend();
  }
  const TreeNode& Oldest() const {
    return list_.front();
  }
  const TreeNode& Latest() const {
    return list_.back();
  }

  std::ranges::subrange FromOldest() {
    return {list_.begin(), list_.end()};
  }
  std::ranges::subrange FromLatest() {
    return {list_.rbegin(), list_.rend()};
  }
  std::ranges::subrange FromNode(tree_iterator it) {
    return {it, list_.rend()};
  }

 private:
  std::list<TreeNode> list_;
  std::unordered_map<Hash, up_iterator> map_;
  Hasher hasher_;
};

}  // namespace hornet::data
