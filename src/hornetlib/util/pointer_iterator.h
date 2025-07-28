// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <concepts>
#include <functional>
#include <iterator>
#include <optional>
#include <type_traits>

namespace hornet::util {

template <typename NodePtr, typename GetNext>
class PointerIterator {
 public:
  // C++20 iterator traits
  using iterator_concept = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::remove_pointer_t<NodePtr>;
  using pointer = NodePtr;
  using reference = value_type&;

  PointerIterator() : node_(nullptr) {}
  explicit PointerIterator(pointer node, GetNext&& fn = GetNext{}) : node_(node), get_next_(std::forward<GetNext>(fn)) {}

  PointerIterator(const PointerIterator&) = default;
  PointerIterator& operator =(const PointerIterator&) = default;
  PointerIterator(PointerIterator&&) = default;
  PointerIterator& operator =(PointerIterator&&) = default;

  reference operator*() const {
    return *node_;
  }
  pointer operator->() const {
    return node_;
  }

  // Increment by invoking the user-provided functor
  PointerIterator& operator++() {
    if (node_) node_ = get_next_(node_);
    return *this;
  }

  PointerIterator operator++(int) {
    PointerIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  bool operator==(const PointerIterator& other) const {
    return node_ == other.node_;
  }
  friend bool operator==(const PointerIterator& it, std::nullptr_t) {
    return it.node_ == nullptr;
  }

private:
  pointer node_;
  
  // May need to wrap get_next_ in a std::optional if default-constructibility becomes an issue.
  [[no_unique_address]] GetNext get_next_;
};

}  // namespace hornet::util
