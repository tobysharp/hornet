// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/parser.h"
#include "hornetlib/util/iterator_range.h"

namespace hornet::protocol::script {

class View {
 public:
  class EofTag {};
  class Iterator {
   public:
    Iterator(lang::Bytes data) : parser_(data), op_(parser_.Next()) {}
    bool operator==(EofTag) const {
      return !op_.has_value();
    }
    bool operator!=(EofTag rhs) const {
      return !operator==(rhs);
    }
    Iterator& operator++() {
      op_ = parser_.Next();
      return *this;
    }
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }
    const lang::Instruction& operator*() const {
      return *op_;
    }
    const lang::Instruction* operator->() const {
      return &*op_;
    }

   private:
    Parser parser_;
    std::optional<lang::Instruction> op_;
  };

  View(std::span<const uint8_t> bytes) : bytes_(bytes) {}

  // Returns an iterable sequence of Instruction objects.
  auto Instructions() const {
    return util::MakeRange<Iterator, EofTag>(bytes_, {});
  }

  // Returns true if the script starts with the given prefix.
  bool StartsWith(std::span<const uint8_t> prefix) const {
    return prefix.size() <= bytes_.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes_.begin());
  }

 private:
  lang::Bytes bytes_;
};

}  // namespace hornet::protocol::script
