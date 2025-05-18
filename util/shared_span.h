#pragma once

#include <memory>
#include <span>
#include <vector>

namespace hornet::util {

template <typename T>
class SharedSpan {
 public:
  template <typename U>
  SharedSpan(std::span<T> span, std::shared_ptr<U> ptr)
      : span_(span), owner_(std::static_pointer_cast<const void>(ptr)) {}

  template <typename T2>
  SharedSpan(std::shared_ptr<const std::vector<T2>> vec_ptr)
      : SharedSpan(std::span<T>{*vec_ptr}, vec_ptr) {}

  template <typename T2>
  SharedSpan(std::shared_ptr<std::vector<T2>> vec_ptr)
      : SharedSpan(std::span<T>{*vec_ptr}, vec_ptr) {}
      
  bool operator!() const {
    return !owner_ || span_.data() == nullptr;
  }
  const std::span<T>& operator*() const {
    return span_;
  }
  const std::span<T>* operator->() const {
    return &span_;
  }
  void Skip(size_t elements) {
    span_ = span_.subspan(elements);
  }

 private:
  std::span<T> span_;
  std::shared_ptr<const void> owner_;
};

}  // namespace hornet::util
