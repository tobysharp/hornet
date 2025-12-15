#pragma once

#include "hornetlib/util/throw.h"

namespace hornet::util {

template <typename T>
requires std::is_trivially_copyable_v<T>
class Optional {
 public:
  Optional(T value) : engaged_(1), value_(std::move(value)) {}
  Optional() : engaged_(0) {}
  Optional(const Optional&) = default;
  Optional(Optional&&) = default;

  Optional& operator=(const Optional&) = default;
  Optional& operator=(Optional&&) = default;
  Optional& operator =(T value) {
    engaged_ = 1;
    value_ = std::move(value);
    return *this;
  }

  explicit operator bool() const { return engaged_ != 0; }
  
  const T& operator*() const { 
    if (engaged_ == 0) ThrowRuntimeError("Optional dereferenced but empty.");
    return value_;
  }

  const T* operator ->() const {
    return &operator*();
  }

  bool HasValue() const { return engaged_ != 0; }

  void Reset() { engaged_ = 0; }

 private:
  uint8_t engaged_;
  T value_;
};

}  // namespace hornet::util
