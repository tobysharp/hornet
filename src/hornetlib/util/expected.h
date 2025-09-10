#pragma once

#include <variant>

namespace hornet::util {

template <typename T, typename Err>
class Expected {
 public:
  Expected(T value) : value_{std::move(value)} {}
  Expected(Err error) : value_{std::move(error)} {}

  bool operator ==(const T& rhs) const { return Value() == rhs; }
  bool operator !=(const T& rhs) const { return Value() != rhs; }
  bool operator ==(const Err& rhs) const { return Error() == rhs; }
  bool operator !=(const Err& rhs) const { return Error() != rhs; }

  bool IsSuccess() const { return std::holds_alternative<T>(value_); }
  operator bool() const { return IsSuccess(); }

  const T& Value() const { return std::get<T>(value_); }
  const T& operator*() const { return Value(); }
  const T* operator->() const { return &Value(); }

  const Err& Error() const { return std::get<Err>(value_); }

 private:
  std::variant<T, Err> value_;
};

}  // namespace hornet::util
