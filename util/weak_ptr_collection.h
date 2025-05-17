#pragma once

#include <list>
#include <memory>

namespace hornet::util {

template <typename T, typename Collection>
class WeakPtrIteratorT {
 public:
  using WeakPtr = std::weak_ptr<std::remove_const_t<T>>;
  using SharedPtr = std::shared_ptr<std::remove_const_t<T>>;
  using Iterator =
      typename std::conditional_t<std::is_const_v<T>, typename Collection::const_iterator,
                                  typename Collection::iterator>;

  WeakPtrIteratorT(Iterator it, Iterator end) : it_(it), end_(end) {
    while (it_ != end_ && (cur_ = it->lock()) == nullptr) ++it_;
  }

  SharedPtr operator*() const {
    return cur_;
  }

  WeakPtrIteratorT& operator++() {
    cur_.reset();
    ++it_;
    while (it_ != end_ && (cur_ = it_->lock()) == nullptr) ++it_;
    return *this;
  }

  bool operator!=(const WeakPtrIteratorT& rhs) const {
    return it_ != rhs.it_;
  }

 private:
  Iterator it_;
  Iterator end_;
  SharedPtr cur_;
};

template <typename T, typename Collection = std::list<std::weak_ptr<T>>>
class WeakPtrCollection {
 public:
  WeakPtrCollection(Collection collection) : collection_(std::move(collection)) {}
  WeakPtrIteratorT<T, Collection> begin() {
    return {collection_.begin(), collection_.end()};
  }
  WeakPtrIteratorT<T, Collection> end() {
    return {collection_.end(), collection_.end()};
  }
  WeakPtrIteratorT<const T, const Collection> begin() const {
    return {collection_.begin(), collection_.end()};
  }
  WeakPtrIteratorT<const T, const Collection> end() const {
    return {collection_.end(), collection_.end()};
  }
  bool empty() const {
    return collection_.empty();
  }
 private:
  Collection collection_;
};

}  // namespace hornet::util
