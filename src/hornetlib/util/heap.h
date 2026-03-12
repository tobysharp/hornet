#pragma once

#include <algorithm>
#include <functional>
#include <vector>

namespace hornet::util {

// A priority queue adapter that supports move-only extraction.
// Compare(x, Top()) evaluates to true for all non-topmost elements x.
template <typename T, typename Compare = std::less<T>>
class Heap {
 public:
  Heap() = default;
  explicit Heap(Compare comp) : comp_(std::move(comp)) {}

  void Push(T item) {
    data_.push_back(std::move(item));
    std::push_heap(data_.begin(), data_.end(), comp_);
  }

  T Pop() {
    // Swap the next element from the front to the back, maintaining heap order, and pop it off the back.
    std::pop_heap(data_.begin(), data_.end(), comp_);
    T item = std::move(data_.back());
    data_.pop_back();
    return item;
  }

  [[nodiscard]] const T& Top() const { return data_.front(); }
  [[nodiscard]] bool Empty() const { return data_.empty(); }
  [[nodiscard]] ssize_t Size() const { return std::ssize(data_); }

 private:
  std::vector<T> data_;
  [[no_unique_address]] Compare comp_;
};

}  // namespace hornet::util
