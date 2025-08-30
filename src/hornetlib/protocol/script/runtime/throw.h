// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <exception>
#include <string>
#include <utility>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/util/throw.h"

namespace hornet::protocol::script::runtime {

class Exception : public std::exception {
 public:
  Exception(lang::Error error, const std::string& msg) noexcept : error_(error), msg_(msg) {}
  virtual ~Exception() noexcept = default;
  virtual const char* what() const noexcept override {
    return msg_.c_str();
  }

  lang::Error GetError() const { return error_; }
 private:
  lang::Error error_;
  std::string msg_;
};

template <typename... Args>
[[noreturn]] inline void Throw(lang::Error error, Args... args) {
  throw Exception{error, util::ToString(std::forward<Args>(args)...)};
}

}  // namespace hornet::protocol::script::runtime
