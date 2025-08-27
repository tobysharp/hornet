#pragma once

#include <exception>
#include <string>
#include <utility>

#include "hornetlib/util/throw.h"

namespace hornet::protocol::script::runtime {

class Exception : public std::exception {
 public:
  Exception(const std::string& msg) noexcept : msg_(msg) {}
  virtual ~Exception() noexcept = default;
  virtual const char* what() const noexcept override {
    return msg_.c_str();
  }

 private:
  std::string msg_;
};

template <typename... Args>
[[noreturn]] inline void Throw(Args... args) {
  throw Exception{ToString(std::forward<Args>(args)...)};
}

}  // namespace hornet::protocol::script::runtime
